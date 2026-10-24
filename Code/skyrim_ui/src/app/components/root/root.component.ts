import { Overlay } from '@angular/cdk/overlay';
import { Component, OnInit, ViewChild } from '@angular/core';
import {
  faArrowRightToBracket,
  faCheck,
  faEye,
  faFlag,
  faGear,
  faList,
  faPowerOff,
  faRotateRight,
  faUsers,
} from '@fortawesome/free-solid-svg-icons';
import { TranslocoService } from '@ngneat/transloco';
import { combineLatest, pairwise, takeUntil } from 'rxjs';
import { environment } from '../../../environments/environment';
import { fadeInOutActiveAnimation } from '../../animations/fade-in-out-active.animation';
import { PlayerManagerTab } from '../../models/player-manager-tab.enum';
import { View } from '../../models/view.enum';
import { ClientService } from '../../services/client.service';
import { DestroyService } from '../../services/destroy.service';
import { ErrorService } from '../../services/error.service';
import { GroupService } from '../../services/group.service';
import { PlayerListService } from '../../services/player-list.service';
import { PopupNotificationService } from '../../services/popup-notification.service';
import {
  SettingService,
  fontSizeToPixels,
} from '../../services/setting.service';
import { Sound, SoundService } from '../../services/sound.service';
import { StoreService } from '../../services/store.service';
import { UiRepository } from '../../store/ui.repository';
import { ChatComponent } from '../chat/chat.component';
import { GroupComponent } from '../group/group.component';
import { controlsAnimation } from './controls.animation';
import { notificationsAnimation } from './notifications.animation';
import { map } from 'rxjs/operators';
import { VoiceService } from '../../services/voice.service';

const REVEAL_EFFECT_DURATION_MS = 10000 // todo: pass value from C++?

@Component({
  selector: 'app-root',
  templateUrl: './root.component.html',
  styleUrls: ['./root.component.scss'],
  animations: [
    controlsAnimation,
    fadeInOutActiveAnimation,
    notificationsAnimation,
  ],
  host: { 'data-app-root-game': environment.game.toString() },
  providers: [DestroyService],
})
export class RootComponent implements OnInit {
  /* ### ENUMS ### */
  readonly RootView = View;
  readonly PlayerManagerTab = PlayerManagerTab;

  readonly icons = {
    rejoin: faRotateRight,
    connect: faArrowRightToBracket,
    servers: faList,
    settings: faGear,
    players: faUsers,
    party: faFlag,
    reveal: faEye,
    done: faCheck,
    disconnect: faPowerOff,
  };

  view$ = this.uiRepository.view$;

  connected$ = this.client.connectionStateChange.asObservable();
  menuOpen$ = this.client.openingMenuChange.asObservable();
  inGame$ = this.client.inGameStateChange.asObservable();
  active$ = this.client.activationStateChange.asObservable();
  connectionInProgress$ = this.client.isConnectionInProgressChange.asObservable();
  version$ = this.client.versionSet.asObservable();
  hasError$ = this.errorService.error$.pipe(map(error => !!error));
  revealing = false;

  playerCount$ = this.playerListService.playerList.pipe(
    map(list => list?.players.length ?? 0),
  );
  playersInfo$ = this.playerCount$.pipe(
    map(count =>
      count === 0
        ? 'COMPONENT.ROOT.PLAYERS_NONE'
        : count === 1
        ? 'COMPONENT.ROOT.PLAYERS_ONE'
        : 'COMPONENT.ROOT.PLAYERS_MANY',
    ),
  );
  inviteCount$ = this.playerListService.playerList.pipe(
    map(list => list?.players.filter(player => player.hasInvitedLocalPlayer).length ?? 0),
  );
  partySize$ = this.groupService.group.pipe(map(group => group?.members.length ?? 0));
  partyInfo$ = this.groupService.group.pipe(
    map(group =>
      !group?.isEnabled
        ? 'COMPONENT.ROOT.PARTY_NONE'
        : group.owner === this.client.localPlayerId
        ? 'COMPONENT.ROOT.PARTY_LEADER'
        : 'COMPONENT.ROOT.PARTY_MEMBER',
    ),
  );

  // The other players and the round trip time, as the status chip shows them.
  connectionDetail$ = combineLatest([
    this.playerCount$,
    this.client.debugDataChange,
  ]).pipe(
    map(([count, debug]) =>
      [this.client.host, `${count + 1} ${this.translocoService.translate('COMPONENT.ROOT.STATUS.PLAYERS')}`, debug.RTT ? `${debug.RTT} ms` : '']
        .filter(part => !!part)
        .join(' · '),
    ),
  );

  @ViewChild('chat') private chatComp!: ChatComponent;
  @ViewChild(GroupComponent) private groupComponent: GroupComponent;

  public constructor(
    private readonly destroy$: DestroyService,
    public readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly uiRepository: UiRepository,
    private readonly translocoService: TranslocoService,
    private readonly settingService: SettingService,
    private readonly errorService: ErrorService,
    private readonly groupService: GroupService,
    private readonly playerListService: PlayerListService,
    private readonly popupNotificationService: PopupNotificationService,
    private readonly storeService: StoreService,
    public readonly overlay: Overlay, // used for mockup
    private readonly voiceService: VoiceService, // sends the voice settings to the game from the start
  ) {
    this.translocoService.setActiveLang(
      this.settingService.settings.language.getValue(),
    );
  }

  /** The last server this menu joined, for the Rejoin button. */
  public get lastServer(): string {
    return this.storeService.get('last_joined_address', '');
  }

  public ngOnInit(): void {
    this.onInGameStateSubscription();
    this.onActivationStateSubscription();
    this.onFontSizeSubscription();
    this.onConnectionToasts();
  }

  public onInGameStateSubscription() {
    this.client.inGameStateChange
      .pipe(takeUntil(this.destroy$))
      .subscribe(state => {
        if (!state) {
          this.closeView();
        }
      });
  }

  public onActivationStateSubscription() {
    this.client.activationStateChange
      .pipe(takeUntil(this.destroy$))
      .subscribe(state => {
        if (
          this.client.inGameStateChange.getValue() &&
          state &&
          !this.uiRepository.isViewOpen()
        ) {
          setTimeout(() => this.chatComp.focus(), 100);
        }
        if (!state) {
          this.closeView();
        }
      });
  }

  public onFontSizeSubscription() {
    this.settingService.settings.fontSize
      .pipe(
        takeUntil(this.destroy$),
        map(size => fontSizeToPixels[size]),
      )
      .subscribe(size => {
        document.documentElement.setAttribute('style', `font-size: ${size}px;`);
      });
  }

  // Joining and leaving show at the top of the panel, whichever window started them.
  public onConnectionToasts() {
    this.client.connectionStateChange
      .pipe(pairwise(), takeUntil(this.destroy$))
      .subscribe(([was, now]) => {
        if (!was && now) {
          this.popupNotificationService.addToast(
            this.client.host ? 'COMPONENT.ROOT.TOAST.CONNECTED_TO' : 'COMPONENT.ROOT.TOAST.CONNECTED',
            { address: this.client.host },
          );
        } else if (was && !now) {
          this.popupNotificationService.addToast('COMPONENT.ROOT.TOAST.DISCONNECTED');
        }
      });
  }

  public setView(view: View | null) {
    this.uiRepository.openView(view);

    if (view) {
      this.sound.play(Sound.Focus);
    } else if (this.chatComp) {
      this.chatComp.focus();
    }
  }

  public openPlayerManager(tab: PlayerManagerTab) {
    this.uiRepository.openPlayerManagerTab(tab);
    this.setView(View.PLAYER_MANAGER);
  }

  public rejoin() {
    this.uiRepository.openConnectNow(this.lastServer);
    this.sound.play(Sound.Focus);
  }

  public closeView() {
    this.uiRepository.openView(null);
  }

  public reconnect(): void {
    this.client.reconnect();
  }

  public revealPlayers(): void {
    if (this.revealing)
      return;

    this.revealing = true;
    setTimeout(() => { this.revealing = false }, REVEAL_EFFECT_DURATION_MS);

    this.sound.play(Sound.Focus);
    this.popupNotificationService.addToast('COMPONENT.ROOT.TOAST.REVEALING');
    this.client.revealPlayers();
  }
}
