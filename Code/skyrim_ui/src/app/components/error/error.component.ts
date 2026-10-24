import { Component, EventEmitter, Output, SecurityContext } from '@angular/core';
import { DomSanitizer } from '@angular/platform-browser';
import { faArrowLeft, faXmark } from '@fortawesome/free-solid-svg-icons';
import { Observable } from 'rxjs';
import { map } from 'rxjs/operators';
import { ErrorService } from '../../services/error.service';
import { UiRepository } from '../../store/ui.repository';

@Component({
  selector: 'app-error',
  templateUrl: './error.component.html',
  styleUrls: ['./error.component.scss'],
})
export class ErrorComponent {
  /** Closes the window the error was raised over. */
  @Output() public closeAll = new EventEmitter<void>();

  readonly faXmark = faXmark;
  readonly faArrowLeft = faArrowLeft;

  error$: Observable<{ title: string; message: string; install: string[]; remove: string[] } | null>;

  constructor(
    private readonly errorService: ErrorService,
    private readonly domSanitizer: DomSanitizer,
    private readonly uiRepository: UiRepository,
  ) {
    this.error$ = this.errorService.error$.pipe(
      map(error =>
        error
          ? {
              ...error,
              message: this.domSanitizer
                .sanitize(SecurityContext.HTML, error.message)
                .replace(/&#10;/g, '<br>'),
            }
          : null,
      ),
    );
  }

  /** Whether a window waits behind the error (the connect window, say), for Back to return to. */
  get windowBehind() {
    return this.uiRepository.isViewOpen();
  }

  back() {
    this.errorService.removeError();
  }

  close() {
    this.errorService.removeError();
    this.closeAll.emit();
  }
}
