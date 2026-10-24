import { Injectable } from '@angular/core';
import { faCheck, faHandshakeSimple, faXmark } from '@fortawesome/free-solid-svg-icons';
import { Subject } from 'rxjs';
import { PopupNotification } from '../models/popup-notification';
import { Sound, SoundService } from './sound.service';

@Injectable({
  providedIn: 'root',
})
export class PopupNotificationService {
  private message = new Subject<PopupNotification>();
  public message$ = this.message.asObservable();
  private messagesCleared = new Subject<void>();
  public messagesCleared$ = this.messagesCleared.asObservable();

  constructor(private readonly soundService: SoundService) {}

  public addMessage(notification: PopupNotification) {
    this.message.next(notification);
    this.soundService.play(Sound.Focus);
  }

  public addPartyInvite(from: string, callback: () => void) {
    this.addMessage({
      messageKey: 'SERVICE.PLAYER_LIST.PARTY_INVITE',
      messageParams: { from },
      icon: faHandshakeSimple,
      // Short: the invite also waits in the party tab, and the Party button counts it.
      duration: 8000,
      actions: [
        {
          nameKey: 'COMPONENT.NOTIFICATIONS.ACCEPT',
          callback,
        },
      ],
    });
  }

  /** A short line at the top of the panel saying that something was done or failed; the action plays its own sound. */
  public addToast(messageKey: string, messageParams?: Record<string, any>, kind: 'ok' | 'bad' = 'ok') {
    this.message.next({
      messageKey,
      messageParams,
      icon: kind === 'ok' ? faCheck : faXmark,
      duration: 4000,
      kind,
    });
  }

  public clearMessages() {
    this.messagesCleared.next();
  }
}
