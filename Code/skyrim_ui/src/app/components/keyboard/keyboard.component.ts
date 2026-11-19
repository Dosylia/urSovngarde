import { Component, HostListener, OnDestroy, OnInit } from '@angular/core';
import { combineLatest, Subject, takeUntil } from 'rxjs';
import { ClientService } from '../../services/client.service';

type TextField = HTMLInputElement | HTMLTextAreaElement;

/**
 * A keyboard on the page, for the menu's panel inside the game in VR, where no system keyboard comes up.
 * It comes up when the player presses on a text field on that panel, never for a field the page focuses itself.
 * It sits on the side away from the field, with a line showing what is typed, and types into the field as real keys would.
 * Its keys act on the press and never take the focus, so a laser that drifts before the release still types, and the field keeps its caret.
 */
@Component({
  selector: 'app-keyboard',
  templateUrl: './keyboard.component.html',
  styleUrls: ['./keyboard.component.scss'],
})
export class KeyboardComponent implements OnInit, OnDestroy {
  readonly letters = [
    ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'],
    ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
    ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'],
    ['z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.'],
  ];
  readonly symbols = [
    ['!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_'],
    ['+', '=', '/', '\\', '|', '[', ']', '{', '}', '~'],
    [':', ';', "'", '"', '<', '>', '?', '`'],
    ['1', '2', '3', '4', '5', '6', '7', '8', '9'],
  ];

  shift = false;
  capsLock = false;
  showSymbols = false;
  atTop = false;
  field: TextField | null = null;
  onPanel = false;

  private lastShift = 0;
  private readonly destroy = new Subject<void>();

  constructor(private readonly client: ClientService) {}

  // A field taken off the page, or hidden behind an error, while it had the focus does not always say so.
  get shown(): boolean {
    const field = this.field as any;
    return this.onPanel && !!field?.isConnected && (typeof field.checkVisibility !== 'function' || field.checkVisibility({ visibilityProperty: true, opacityProperty: true }));
  }

  get rows(): string[][] {
    return this.showSymbols ? this.symbols : this.letters;
  }

  get capitals(): boolean {
    return this.shift !== this.capsLock;
  }

  // What is typed, as the field would show it, for a field the keyboard may cover; its end, where the typing is.
  get echo(): string {
    const field = this.field;
    if (!field) {
      return '';
    }
    const text = field instanceof HTMLInputElement && field.type === 'password' ? '•'.repeat(field.value.length) : field.value;
    return text.length > 48 ? '…' + text.slice(-47) : text;
  }

  ngOnInit(): void {
    combineLatest([this.client.activationStateChange, this.client.surfaceChange])
      .pipe(takeUntil(this.destroy))
      .subscribe(([active, surface]) => {
        this.onPanel = active && surface === 'panel';
        if (!this.onPanel) {
          this.reset();
        }
      });
  }

  ngOnDestroy(): void {
    this.destroy.next();
    this.destroy.complete();
  }

  // A press on a text field brings the keyboard up; a field focused by the page itself does not.
  @HostListener('document:mousedown', ['$event'])
  onPress(event: MouseEvent): void {
    const target = event.target;
    // Only presses on the panel: a field pressed on another surface does not bring the keyboard up when the menu moves there.
    if (!this.onPanel || (target instanceof Node && this.contains(target))) {
      return;
    }
    const field = this.asTextField(target);
    if (field) {
      if (field !== this.field) {
        this.reset();
      }
      this.field = field;
      const box = field.getBoundingClientRect();
      this.atTop = box.top + box.height / 2 > window.innerHeight / 2;
    } else {
      this.reset();
    }
  }

  @HostListener('document:focusout', ['$event'])
  onFocusOut(event: FocusEvent): void {
    // The keyboard's own keys never take the focus; anything else that does ends the typing.
    if (event.target === this.field && !(event.relatedTarget instanceof Node && this.contains(event.relatedTarget))) {
      this.reset();
    }
  }

  // Each key acts on the press; stopping the press keeps the focus on the field.
  press(event: MouseEvent): void {
    event.preventDefault();
    event.stopPropagation();
  }

  label(key: string): string {
    return this.capitals ? key.toUpperCase() : key;
  }

  type(key: string): void {
    this.insert(this.label(key));
    this.shift = false;
  }

  // Once for one capital; twice quickly for all of them, until pressed again.
  toggleShift(): void {
    const now = Date.now();
    if (this.capsLock) {
      this.capsLock = false;
      this.shift = false;
    } else if (this.shift && now - this.lastShift < 400) {
      this.capsLock = true;
      this.shift = false;
    } else {
      this.shift = !this.shift;
    }
    this.lastShift = now;
  }

  toggleSymbols(): void {
    this.showSymbols = !this.showSymbols;
  }

  space(): void {
    this.insert(' ');
  }

  backspace(): void {
    const field = this.field;
    if (!field) {
      return;
    }
    const start = field.selectionStart ?? field.value.length;
    const end = field.selectionEnd ?? start;
    if (start !== end) {
      field.setRangeText('', start, end, 'end');
    } else if (start > 0) {
      field.setRangeText('', start - 1, start, 'end');
    } else {
      // Nothing to take away: a real key would not tell the page anything changed either.
      return;
    }
    this.changed(field);
  }

  enter(): void {
    const field = this.field;
    if (!field) {
      return;
    }
    // As the real key: the page's (keydown.enter) handlers send the chat line or join.
    for (const type of ['keydown', 'keyup']) {
      field.dispatchEvent(new KeyboardEvent(type, { key: 'Enter', code: 'Enter', bubbles: true }));
    }
  }

  close(): void {
    this.field?.blur();
    this.reset();
  }

  private reset(): void {
    this.field = null;
    this.shift = false;
    this.capsLock = false;
    this.showSymbols = false;
  }

  private insert(text: string): void {
    const field = this.field;
    if (!field) {
      return;
    }
    const max = field.maxLength > 0 ? field.maxLength : Infinity;
    const start = field.selectionStart ?? field.value.length;
    const end = field.selectionEnd ?? start;
    if (field.value.length - (end - start) + text.length > max) {
      return;
    }
    field.setRangeText(text, start, end, 'end');
    this.changed(field);
  }

  // Angular's forms listen for input events, as typing makes them.
  private changed(field: TextField): void {
    field.dispatchEvent(new Event('input', { bubbles: true }));
  }

  private asTextField(target: EventTarget | null): TextField | null {
    if (target instanceof HTMLTextAreaElement) {
      return target.readOnly || target.disabled ? null : target;
    }
    if (target instanceof HTMLInputElement && ['text', 'password', 'search', ''].includes(target.type)) {
      return target.readOnly || target.disabled ? null : target;
    }
    return null;
  }

  private contains(node: Node): boolean {
    return !!document.querySelector('app-keyboard')?.contains(node);
  }
}
