import {
  Component,
  EventEmitter,
  Input,
  OnDestroy,
  OnInit,
  Output,
} from '@angular/core';

/** What the connect windows show while the game waits for the server: the address, the time so far, and a way out. */
@Component({
  selector: 'app-connecting',
  templateUrl: './connecting.component.html',
  styleUrls: ['./connecting.component.scss'],
})
export class ConnectingComponent implements OnInit, OnDestroy {
  @Input() public address = '';
  @Output() public cancel = new EventEmitter<void>();

  public seconds = 0;

  private timer: ReturnType<typeof setInterval>;

  public ngOnInit(): void {
    const start = Date.now();
    this.timer = setInterval(() => {
      this.seconds = Math.floor((Date.now() - start) / 1000);
    }, 250);
  }

  public ngOnDestroy(): void {
    clearInterval(this.timer);
  }
}
