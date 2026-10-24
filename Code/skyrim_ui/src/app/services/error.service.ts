import { Injectable } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject, firstValueFrom } from 'rxjs';
import { Sound, SoundService } from './sound.service';

export interface ErrorEvent {
  error:
    | 'wrong_version'
    | 'mods_mismatch'
    | 'client_mods_disallowed'
    | 'wrong_password'
    | 'server_full'
    | 'no_reason'
    | 'bad_uGridsToLoad'
    | 'non_default_install'
    | 'set_time_public_server';
  data?: Record<any, any>;
}

export interface WrongVersionErrorEvent extends ErrorEvent {
  error: 'wrong_version';
  data: { version: string; expectedVersion: string };
}

export interface ModsMismatchErrorEvent extends ErrorEvent {
  error: 'mods_mismatch';
  data: { mods: [filename: string, id: string][] };
}

export interface ClientModsDisallowedErrorEvent extends ErrorEvent {
  error: 'client_mods_disallowed';
  data: { mods: ('SKSE' | 'MO2')[] };
}

export type ErrorEvents =
  | WrongVersionErrorEvent
  | ModsMismatchErrorEvent
  | ClientModsDisallowedErrorEvent
  | ErrorEvent;

/** An error as the error window shows it: a title, the text, and for a mod list that differs, what to change. */
export interface ShownError {
  title: string;
  message: string;
  install: string[];
  remove: string[];
}

// Errors the server or the game raise while connecting; the others get the general title.
const connectErrors = ['wrong_version', 'client_mods_disallowed', 'wrong_password', 'server_full', 'no_reason'];
const setupErrors = ['bad_uGridsToLoad', 'non_default_install'];

@Injectable({
  providedIn: 'root',
})
export class ErrorService {
  private error = new BehaviorSubject<ShownError | null>(null);
  public error$ = this.error.asObservable();

  constructor(
    private readonly sound: SoundService,
    private readonly translocoService: TranslocoService,
  ) {}

  getError() {
    return this.error.getValue()?.message ?? '';
  }

  /** Shows an error; a plain message takes the title given, or the general one. */
  async setError(error: ErrorEvents | string, titleKey = 'SERVICE.ERROR.TITLES.DEFAULT') {
    this.sound.play(Sound.Fail);
    let message: string;
    let install: string[] = [];
    let remove: string[] = [];
    if (typeof error === 'string') {
      message = error;
    } else {
      let data: Record<string, any> = error.data;
      let messageKey = 'SERVICE.ERROR.ERRORS.' + error.error.toUpperCase();
      if (connectErrors.includes(error.error)) {
        titleKey = 'SERVICE.ERROR.TITLES.CONNECT';
      } else if (setupErrors.includes(error.error)) {
        titleKey = 'SERVICE.ERROR.TITLES.SETUP';
      }
      switch (error.error) {
        case 'mods_mismatch':
          titleKey = 'SERVICE.ERROR.TITLES.MODS_MISMATCH';
          messageKey = 'SERVICE.ERROR.ERRORS.MODS_MISMATCH_HELP';
          install = error.data.mods.filter(mod => mod[1] === '0').map(mod => mod[0]);
          remove = error.data.mods.filter(mod => mod[1] !== '0').map(mod => mod[0]);
          break;
        case 'client_mods_disallowed':
          data = { mods: error.data.mods.join(', ') };
          break;
      }
      message = await firstValueFrom(this.translocoService.selectTranslate(messageKey, data));
    }
    const title = await firstValueFrom(this.translocoService.selectTranslate(titleKey));
    this.error.next({ title, message, install, remove });
  }

  removeError() {
    this.sound.play(Sound.Ok);
    this.error.next(null);
  }
}
