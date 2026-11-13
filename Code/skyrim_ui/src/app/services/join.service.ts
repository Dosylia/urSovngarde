import { Injectable, NgZone, OnDestroy } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject, Subject, Subscription, filter } from 'rxjs';
import { ClientService } from './client.service';
import { ErrorService } from './error.service';
import { SettingService } from './setting.service';

/** A Steam friend playing urSovngarde, as the game reads them from Steam. */
export interface SteamFriend {
  /** The Steam id, as text: JavaScript cannot hold 64 bits. */
  id: string;
  name: string;
  /** On their own server. */
  hosting: boolean;
  /** On another player's server. */
  onServer: boolean;
  /** This PC can reach that server without the launcher. */
  reachable: boolean;
  /** How: 'lan', 'vlan', 'public' (only with their port open) or 'server'; empty when out of reach. */
  route: string;
  password: boolean;
  /** The launcher's code, while they host with it; empty otherwise. */
  code: string;
}

/** The launcher's answer to the last join it was asked for. */
export interface LauncherAnswer {
  request: number;
  state: 'joining' | 'ready' | 'password' | 'failed';
  code: string;
  address: string | null;
  reason: string | null;
  detail: string | null;
}

/** The launcher's launcher.json, contract 1. */
export interface LauncherState {
  contract: number;
  written: number;
  launcher: string;
  hosting: { code: string; password: boolean } | null;
  join: LauncherAnswer | null;
}

/** Where the menu's join is, from the request to the connection. */
export interface JoinStep {
  step: 'asking' | 'connecting' | 'password' | 'failed';
  code: string;
  name: string;
  reason?: string;
}

/** The launcher's reasons, as serde names them, and ours, with the words for each. */
const REASONS: Record<string, string> = {
  noSuchCode: 'NO_SUCH_CODE',
  tooMany: 'TOO_MANY',
  refused: 'REFUSED',
  noKey: 'REFUSED',
  unreachable: 'UNREACHABLE',
  relay: 'RELAY',
  wrongVersion: 'WRONG_VERSION',
  offline: 'OFFLINE',
  noAnswer: 'NO_ANSWER',
  notReached: 'NOT_REACHED',
  notReachable: 'NOT_REACHABLE',
};

/** The words for a failed join. */
export function reasonKey(reason: string | undefined): string {
  return `COMPONENT.JOIN.ERROR.${REASONS[reason ?? ''] ?? 'OTHER'}`;
}

/** The launcher rewrites its state every 5 s, so a state older than this is from a launcher that has closed. */
const LAUNCHER_SILENT_MS = 15000;

/** Until the launcher takes the request; the launcher ignores one older than this. */
const TAKEN_TIMEOUT_MS = 30000;

/** Until it has looked the code up and set up the relay, once taken. */
const ANSWER_TIMEOUT_MS = 90000;

/** Until the game is on the friend's server, once the launcher wrote it. */
const CONNECT_TIMEOUT_MS = 60000;

/** Until the game is on a Steam friend's server: it tries up to three addresses, 30 s each. */
const STEAM_CONNECT_TIMEOUT_MS = 100000;

/**
 * Joins friends: Steam friends straight from the game, and codes through the urSovngarde launcher, which holds the
 * codes and the relay tunnel.
 * The game passes the launcher's state as it is; the launcher writes the friend's server into the game's connect file.
 * The join lives here, not in the panel: closing the dashboard closes the panel, and the join goes on.
 * How it ends also shows on the HUD, for a player who closed the dashboard.
 */
@Injectable({ providedIn: 'root' })
export class JoinService implements OnDestroy {
  /** Steam friends playing urSovngarde who show where they play. */
  public readonly friends = new BehaviorSubject<SteamFriend[]>([]);

  /** The launcher's state while it runs, null when it is closed or too old for this menu. */
  public readonly state = new BehaviorSubject<LauncherState | null>(null);

  /** The menu's join; null when there is none. */
  public readonly join = new BehaviorSubject<JoinStep | null>(null);

  /** The game reached the friend's server. */
  public readonly joined = new Subject<void>();

  private last: LauncherState | null = null;
  private request = 0;
  // The launcher took the request; it repeats that answer every 5 s, and the longer wait starts only once.
  private taken = false;
  private timer?: ReturnType<typeof setTimeout>;
  private readonly subscriptions = new Subscription();
  // Outside Angular, so the page is not redrawn every 2 s for nothing.
  private readonly freshness = this.zone.runOutsideAngular(() =>
    setInterval(() => {
      if (this.fresh() !== this.state.value) {
        this.zone.run(() => this.publish());
      }
    }, 2000),
  );

  public constructor(
    private readonly zone: NgZone,
    private readonly client: ClientService,
    private readonly errorService: ErrorService,
    private readonly translocoService: TranslocoService,
    settingService: SettingService,
  ) {
    skyrimtogether.on('launcherState', this.onState.bind(this));
    skyrimtogether.on('steamFriends', this.onSteamFriends.bind(this));
    skyrimtogether.on('steamJoinFailed', this.onSteamJoinFailed.bind(this));

    // The game shows Steam friends where this player plays only once the menu says so, from the start.
    this.subscriptions.add(
      settingService.settings.shareWithSteamFriends.subscribe(sharing =>
        skyrimtogether.setShareWithSteamFriends(sharing),
      ),
    );

    // Leaving the old server reads as a disconnection; the menu still shows the join going on.
    this.subscriptions.add(
      this.client.connectionStateChange.subscribe(connected => {
        if (this.join.value?.step !== 'connecting') {
          return;
        }
        if (connected) {
          this.end(null);
          this.joined.next();
        } else {
          this.client.isConnectionInProgressChange.next(true);
        }
      }),
    );

    // A refusal (password, version, mods) shows as the menu's error, which says why.
    this.subscriptions.add(
      this.errorService.error$
        .pipe(filter(error => !!error))
        .subscribe(() => {
          if (this.join.value?.step === 'connecting') {
            this.end(null);
          }
        }),
    );
  }

  public ngOnDestroy(): void {
    clearInterval(this.freshness);
    clearTimeout(this.timer);
    this.subscriptions.unsubscribe();
    skyrimtogether.off('launcherState');
    skyrimtogether.off('steamFriends');
    skyrimtogether.off('steamJoinFailed');
  }

  /** Joins the server a Steam friend is on, or hosts, straight from the game: no launcher. */
  public joinSteamFriend(friend: SteamFriend, password = ''): void {
    this.withdraw();
    this.request = 0;
    this.join.next({ step: 'connecting', code: '', name: friend.name });
    this.client.joiningFriend(friend.name);
    this.wait(STEAM_CONNECT_TIMEOUT_MS, 'notReached');
    skyrimtogether.joinSteamFriend(friend.id, password);
  }

  /** Asks the launcher to join a code; its answer comes back in its state. */
  public joinCode(code: string, name = '', password = ''): void {
    this.request = Date.now();
    this.taken = false;
    this.join.next({ step: 'asking', code, name });
    this.wait(TAKEN_TIMEOUT_MS, 'noAnswer');
    skyrimtogether.launcherJoin(String(this.request), code, name, password);
  }

  /** Forgets the join; a request the launcher has not taken yet is withdrawn. */
  public clearJoin(): void {
    this.withdraw();
    this.end(null);
  }

  /** Stops a join while the game connects: no more attempts at the friend's server. */
  public cancelConnecting(): void {
    this.end(null);
    this.client.disconnect();
    this.client.isConnectionInProgressChange.next(false);
  }

  private onState(text: string): void {
    this.zone.run(() => {
      this.last = this.read(text);
      this.publish();
      this.answer(this.last?.join ?? null);
    });
  }

  /** The game found no way to the friend's server: it said so on the HUD and left the player where they are. */
  private onSteamJoinFailed(): void {
    this.zone.run(() => {
      const step = this.join.value;
      if (step?.step === 'connecting' && !this.request) {
        this.client.isConnectionInProgressChange.next(false);
        this.end({ ...step, step: 'failed', reason: 'notReachable' });
      }
    });
  }

  private onSteamFriends(text: string): void {
    this.zone.run(() => {
      let parsed: any;
      try {
        parsed = JSON.parse(text);
      } catch {
        return;
      }
      const friends: SteamFriend[] = Array.isArray(parsed?.friends)
        ? parsed.friends
            .filter((f: any) => typeof f?.id === 'string' && typeof f?.name === 'string')
            .map((f: any) => ({
              id: f.id,
              name: f.name,
              hosting: !!f.hosting,
              onServer: !!f.onServer,
              reachable: !!f.reachable,
              route: typeof f.route === 'string' ? f.route : '',
              password: !!f.password,
              code: typeof f.code === 'string' ? f.code : '',
            }))
        : [];
      if (JSON.stringify(friends) !== JSON.stringify(this.friends.value)) {
        this.friends.next(friends);
      }
    });
  }

  /** The launcher's file, kept only when it has the shape of contract 1. */
  private read(text: string): LauncherState | null {
    let parsed: any;
    try {
      parsed = text ? JSON.parse(text) : null;
    } catch {
      return null;
    }
    if (parsed?.contract !== 1 || typeof parsed.written !== 'number') {
      return null;
    }

    const isText = (value: unknown) => typeof value === 'string' && value !== '';
    const hosting = isText(parsed.hosting?.code)
      ? { code: parsed.hosting.code, password: !!parsed.hosting.password }
      : null;
    const join = typeof parsed.join?.request === 'number' ? parsed.join : null;
    return { contract: 1, written: parsed.written, launcher: String(parsed.launcher ?? ''), hosting, join };
  }

  private fresh(): LauncherState | null {
    return this.last && Date.now() - this.last.written * 1000 < LAUNCHER_SILENT_MS
      ? this.last
      : null;
  }

  private publish(): void {
    const fresh = this.fresh();
    if (fresh === this.state.value) {
      return;
    }

    this.state.next(fresh);
    // The launcher closed while it had the request: nobody will answer it.
    const step = this.join.value;
    if (!fresh && step?.step === 'asking') {
      this.fail(step, 'noAnswer');
    }
  }

  private answer(answer: LauncherAnswer | null): void {
    const step = this.join.value;
    if (!step || step.step !== 'asking' || !answer || answer.request !== this.request) {
      return;
    }

    switch (answer.state) {
      case 'joining':
        if (!this.taken) {
          this.taken = true;
          this.wait(ANSWER_TIMEOUT_MS, 'noAnswer');
        }
        return;
      case 'ready':
        this.join.next({ ...step, step: 'connecting' });
        this.client.joiningFriend(step.name || step.code);
        this.wait(CONNECT_TIMEOUT_MS, 'notReached');
        skyrimtogether.connectFromLauncher();
        return;
      case 'password':
        this.end({ ...step, step: 'password' });
        this.tellHud('COMPONENT.JOIN.HUD_PASSWORD');
        return;
      default:
        this.fail(step, answer.reason ?? '');
    }
  }

  /** The join fails with this reason unless it moves on first; a friend's server that never answers is left. */
  private wait(milliseconds: number, reason: string): void {
    clearTimeout(this.timer);
    this.timer = setTimeout(
      () =>
        this.zone.run(() => {
          const step = this.join.value;
          if (step?.step === 'connecting') {
            this.client.disconnect();
            this.client.isConnectionInProgressChange.next(false);
          }
          if (step?.step === 'asking' || step?.step === 'connecting') {
            this.fail(step, reason);
          }
        }),
      milliseconds,
    );
  }

  private fail(step: JoinStep, reason: string): void {
    if (step.step === 'asking') {
      this.withdraw();
    }
    this.end({ ...step, step: 'failed', reason });
    this.tellHud(reasonKey(reason));
  }

  /** A request the launcher has not taken yet is taken back, so it cannot change the connect file later. */
  private withdraw(): void {
    if (this.join.value?.step === 'asking') {
      skyrimtogether.launcherCancel();
    }
  }

  private tellHud(key: string): void {
    skyrimtogether.showHudMessage(this.translocoService.translate(key));
  }

  private end(step: JoinStep | null): void {
    clearTimeout(this.timer);
    this.request = step ? this.request : 0;
    this.join.next(step);
  }
}
