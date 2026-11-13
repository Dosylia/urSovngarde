import { Component, EventEmitter, OnDestroy, Output } from '@angular/core';
import { Subscription } from 'rxjs';
import {
  JoinService,
  JoinStep,
  SteamFriend,
  reasonKey,
} from '../../services/join.service';
import { Sound, SoundService } from '../../services/sound.service';

@Component({
  selector: 'app-join',
  templateUrl: './join.component.html',
  styleUrls: ['./join.component.scss'],
})
export class JoinComponent implements OnDestroy {
  public code = '';
  public password = '';
  public askPassword = false;

  /** The friend picked from the list: their name, kept for a code retry. */
  private friendName = '';
  /** A Steam friend whose server asks for a password: joined once it is typed. */
  private steamFriend: SteamFriend | null = null;

  @Output() public done = new EventEmitter<void>();

  public readonly launcher$ = this.joinService.state.asObservable();
  public readonly friends$ = this.joinService.friends.asObservable();
  public step: JoinStep | null = null;

  private readonly subscriptions = new Subscription();

  public constructor(
    private readonly joinService: JoinService,
    private readonly sound: SoundService,
  ) {
    this.subscriptions.add(
      this.joinService.join.subscribe(step => this.onStep(step)),
    );
    this.subscriptions.add(
      this.joinService.joined.subscribe(() => {
        this.sound.play(Sound.Success);
        this.done.next();
      }),
    );
  }

  public ngOnDestroy(): void {
    this.subscriptions.unsubscribe();
    // A join going on keeps going with the panel closed; one that ended is not shown again.
    if (this.step?.step === 'failed' || this.step?.step === 'password') {
      this.joinService.clearJoin();
    }
  }

  public get busy(): boolean {
    return this.step?.step === 'asking' || this.step?.step === 'connecting';
  }

  public get reasonKey(): string {
    return reasonKey(this.step?.reason);
  }

  public get canJoinTyped(): boolean {
    return !!this.steamFriend || !!this.code.trim();
  }

  public trackFriend(_index: number, friend: SteamFriend): string {
    return friend.id;
  }

  /** Straight from the game when this PC reaches the friend's server, otherwise through the launcher's code. */
  public canJoin(friend: SteamFriend, launcherOpen: boolean): boolean {
    return friend.reachable || (!!friend.code && launcherOpen);
  }

  public whereKey(friend: SteamFriend, launcherOpen: boolean): string {
    if (friend.reachable) {
      return friend.hosting ? 'COMPONENT.JOIN.HOSTING' : 'COMPONENT.JOIN.ON_SERVER';
    }
    if (friend.code) {
      return launcherOpen ? 'COMPONENT.JOIN.HOSTING_LAUNCHER' : 'COMPONENT.JOIN.NEEDS_LAUNCHER';
    }
    return 'COMPONENT.JOIN.NOT_REACHABLE';
  }

  public joinFriend(friend: SteamFriend, launcherOpen: boolean): void {
    this.friendName = friend.name;
    this.steamFriend = null;
    this.password = '';
    this.askPassword = false;

    // A launcher host's public address works only with their port open; the launcher's relay does without.
    const viaCode = !!friend.code && launcherOpen && (!friend.reachable || friend.route === 'public');
    if (friend.reachable && !viaCode) {
      if (friend.password) {
        this.steamFriend = friend;
        this.askPassword = true;
        this.sound.play(Sound.Focus);
        return;
      }
      this.sound.play(Sound.Ok);
      this.joinService.joinSteamFriend(friend);
    } else if (viaCode) {
      this.code = friend.code;
      this.sound.play(Sound.Ok);
      this.joinService.joinCode(friend.code, friend.name);
    }
  }

  public joinTyped(): void {
    if (this.busy || !this.canJoinTyped) {
      return;
    }

    this.sound.play(Sound.Ok);
    if (this.steamFriend) {
      this.joinService.joinSteamFriend(this.steamFriend, this.password);
      this.steamFriend = null;
      return;
    }
    this.joinService.joinCode(this.code.trim(), this.friendName, this.password);
  }

  /** The code field changed by hand: it is no longer the friend's from the list. */
  public onCodeTyped(): void {
    this.friendName = '';
    this.steamFriend = null;
    this.askPassword = false;
    this.password = '';
  }

  public cancelConnecting(): void {
    this.sound.play(Sound.Cancel);
    this.joinService.cancelConnecting();
  }

  /** Closing while the launcher looks the code up gives the join up. */
  public close(): void {
    this.sound.play(Sound.Cancel);
    if (this.step?.step === 'asking') {
      this.joinService.clearJoin();
    }
    this.done.next();
  }

  private onStep(step: JoinStep | null): void {
    this.step = step;
    if (step?.step === 'password') {
      this.code = step.code;
      this.friendName = step.name;
      this.steamFriend = null;
      this.askPassword = true;
      this.sound.play(Sound.Focus);
    } else if (step?.step === 'failed') {
      this.sound.play(Sound.Fail);
    }
  }
}
