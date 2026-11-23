import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { environment } from '../../environments/environment';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { catchError, tap } from 'rxjs/operators';

export interface ThemeSettings {
  colorScheme: string;
  accentColors?: {
    [key: string]: string;
  };
}

@Injectable({
  providedIn: 'root'
})
export class ThemeService {
  private readonly mockSettings: ThemeSettings = {
    colorScheme: 'dark',
    accentColors: {
      '--primary-color': '#E0A000',
      '--primary-color-text': '#ffffff',
      '--highlight-bg': '#E0A000',
      '--highlight-text-color': '#ffffff',
      '--focus-ring': '0 0 0 0.2rem rgba(224,160,0,0.2)',
      // PrimeNG Slider
      '--slider-bg': '#dee2e6',
      '--slider-range-bg': '#E0A000',
      '--slider-handle-bg': '#E0A000',
      // Progress Bar
      '--progressbar-bg': '#dee2e6',
      '--progressbar-value-bg': '#E0A000',
      // PrimeNG Checkbox
      '--checkbox-border': '#E0A000',
      '--checkbox-bg': '#E0A000',
      '--checkbox-hover-bg': '#C58D00',
      // PrimeNG Button
      '--button-bg': '#E0A000',
      '--button-hover-bg': '#C58D00',
      '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #E0A000',
      // Toggle button
      '--togglebutton-bg': '#E0A000',
      '--togglebutton-border': '1px solid #E0A000',
      '--togglebutton-hover-bg': '#C58D00',
      '--togglebutton-hover-border': '1px solid #C58D00',
      '--togglebutton-text-color': '#ffffff'
    }
  };

  private themeSettingsSubject = new BehaviorSubject<ThemeSettings>(this.mockSettings);
  private themeSettings$ = this.themeSettingsSubject.asObservable();

  constructor(private http: HttpClient) {
    if (environment.production) {
      this.http.get<ThemeSettings>('/api/theme').pipe(
        catchError(() => of(this.mockSettings)),
        tap(settings => this.themeSettingsSubject.next(settings))
      ).subscribe();
    }
  }

  getThemeSettings(): Observable<ThemeSettings> {
    return this.themeSettings$;
  }

  saveThemeSettings(settings: ThemeSettings): Observable<void> {
    if (environment.production) {
      return this.http.post<void>('/api/theme', settings).pipe(
        tap(() => this.themeSettingsSubject.next(settings))
      );
    } else {
      this.themeSettingsSubject.next(settings);
      return of(void 0);
    }
  }
}
