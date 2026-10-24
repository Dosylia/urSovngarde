import {
  ChangeDetectionStrategy,
  Component,
  EventEmitter,
  HostListener,
  Output,
} from '@angular/core';
import { faArrowLeft } from '@fortawesome/free-solid-svg-icons';
import { map } from 'rxjs/operators';
import { PlayerManagerTab } from '../../models/player-manager-tab.enum';
import { PlayerListService } from '../../services/player-list.service';
import { Sound, SoundService } from '../../services/sound.service';
import { UiRepository } from '../../store/ui.repository';

@Component({
  selector: 'app-player-manager',
  templateUrl: './player-manager.component.html',
  styleUrls: ['./player-manager.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class PlayerManagerComponent {
  /* ### ENUMS ### */
  readonly PlayerManagerTab = PlayerManagerTab;

  readonly faArrowLeft = faArrowLeft;

  activeTab$ = this.uiRepository.playerManagerTab$;
  playerCount$ = this.playerListService.playerList.pipe(map(list => list?.players.length ?? 0));
  inviteCount$ = this.playerListService.playerList.pipe(
    map(list => list?.players.filter(player => player.hasInvitedLocalPlayer).length ?? 0),
  );

  @Output() public done = new EventEmitter<void>();

  constructor(
    private readonly sound: SoundService,
    private readonly uiRepository: UiRepository,
    private readonly playerListService: PlayerListService,
  ) {}

  switchTab(tab: PlayerManagerTab) {
    this.uiRepository.openPlayerManagerTab(tab);
    this.sound.play(Sound.Focus);
  }

  close() {
    this.done.next();
    this.sound.play(Sound.Ok);
  }

  @HostListener('window:keydown.escape', ['$event'])
  // @ts-ignore
  private activate(event: KeyboardEvent): void {
    this.close();
    event.stopPropagation();
    event.preventDefault();
  }
}
