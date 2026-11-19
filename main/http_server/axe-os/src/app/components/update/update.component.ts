import { Component, ViewChild } from '@angular/core';
import { Observable, TimeoutError, take, timeout } from 'rxjs';
import { HttpErrorResponse, HttpEventType } from '@angular/common/http';
import { ToastrService } from 'ngx-toastr';
import { FirmwareUpdateService } from 'src/app/services/firmware-update.service';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { ModalComponent } from '../modal/modal.component';
import { SystemInfo } from 'src/app/generated/models';
import { PublishedFirmware } from 'src/app/services/firmware-update-source';

type CheckStatus = 'idle' | 'checking' | 'current' | 'available' | 'error';

@Component({
  selector: 'app-update',
  templateUrl: './update.component.html',
  styleUrls: ['./update.component.scss']
})
export class UpdateComponent {

  public firmwareUpdateProgress: number = 0;
  public websiteUpdateProgress: number = 0;

  public info$: Observable<SystemInfo>;

  public checkStatus: CheckStatus = 'idle';
  public checkMessage = '';
  public deviceVersion = '';
  public published: PublishedFirmware | null = null;

  @ViewChild('progressModal') progressModal?: ModalComponent;

  public updatePhase: 'www' | 'firmware' = 'www';
  public updateStatus: 'idle' | 'progress' | 'success' | 'error' = 'idle';
  public updateMessage = '';

  constructor(
    private systemService: SystemApiService,
    private liveDataService: LiveDataService,
    private toastrService: ToastrService,
    private firmwareUpdateService: FirmwareUpdateService,
  ) {
    this.info$ = this.liveDataService.info$;
  }

  public checkForUpdate(): void {
    if (this.checkStatus === 'checking' || this.updateStatus === 'progress') {
      return;
    }
    this.checkStatus = 'checking';
    this.checkMessage = '';
    this.published = null;

    this.info$.pipe(take(1), timeout(8000)).subscribe({
      next: (info) => {
        this.deviceVersion = (info?.version || '').trim();
        this.firmwareUpdateService.check().subscribe({
          next: (published) => {
            this.published = published;
            if (published.version === this.deviceVersion) {
              this.checkStatus = 'current';
              this.checkMessage = `Version ${published.version} on the Satoshiware OS server matches this miner.`;
              return;
            }
            this.checkStatus = 'available';
            this.checkMessage = `The Satoshiware OS server has version ${published.version}. This miner is ${this.deviceVersion || 'unknown'}.`;
          },
          error: (err) => {
            this.checkStatus = 'error';
            this.checkMessage = this.explainCheckError(err);
          },
        });
      },
      error: (err) => {
        this.checkStatus = 'error';
        this.checkMessage = err instanceof TimeoutError
          ? 'The miner version is not available yet.'
          : 'The miner version could not be read.';
      },
    });
  }

  public installPublished(): void {
    const published = this.published;
    if (!published || this.checkStatus !== 'available' || this.updateStatus === 'progress') {
      return;
    }

    this.updatePhase = 'www';
    this.updateStatus = 'progress';
    this.updateMessage = '';
    this.websiteUpdateProgress = 0;
    this.firmwareUpdateProgress = 0;
    if (this.progressModal) {
      this.progressModal.isVisible = true;
    }

    this.firmwareUpdateService.download(published.wwwUrl).subscribe({
      next: (www) => this.sendWebPage(www, published),
      error: () => this.failUpdate('Could not download the web page from the Satoshiware OS server.'),
    });
  }

  private sendWebPage(file: Blob, published: PublishedFirmware): void {
    this.updatePhase = 'www';
    this.systemService.performWWWOTAUpdate(file).subscribe({
      next: (event) => {
        if (event.type === HttpEventType.UploadProgress && event.total) {
          this.websiteUpdateProgress = Math.round((event.loaded / event.total) * 100);
        } else if (event.type === HttpEventType.Response && event.ok) {
          this.downloadFirmware(published);
        } else if (event.type === HttpEventType.Response) {
          this.failUpdate('The web page was not accepted.');
        }
      },
      error: (err) => this.failUpdate(this.httpMessage(err, 'The web page update failed.')),
    });
  }

  private downloadFirmware(published: PublishedFirmware): void {
    this.updatePhase = 'firmware';
    this.firmwareUpdateService.download(published.firmwareUrl).subscribe({
      next: (firmware) => this.sendFirmware(firmware),
      error: () => this.failUpdate('The web page was updated. The firmware could not be downloaded.'),
    });
  }

  private sendFirmware(file: Blob): void {
    this.systemService.performOTAUpdate(file).subscribe({
      next: (event) => {
        if (event.type === HttpEventType.UploadProgress && event.total) {
          this.firmwareUpdateProgress = Math.round((event.loaded / event.total) * 100);
        } else if (event.type === HttpEventType.Response && event.ok) {
          this.updateStatus = 'success';
          this.updateMessage = 'Update installed. The miner is restarting. Hard-refresh this page once it is back.';
          this.toastrService.success('Device restarted');
        } else if (event.type === HttpEventType.Response) {
          this.failUpdate('The web page was updated. The firmware was not accepted.');
        }
      },
      error: (err) => this.failUpdate(this.httpMessage(err, 'The web page was updated. The firmware update failed.')),
    });
  }

  private failUpdate(message: string): void {
    this.updateStatus = 'error';
    this.updateMessage = message;
  }

  private explainCheckError(err: unknown): string {
    if (err instanceof Error && err.message === 'The update server path is not set.') {
      return err.message;
    }
    if (err instanceof Error && err.message.startsWith('The Satoshiware OS server')) {
      return err.message;
    }
    const status = err instanceof HttpErrorResponse ? err.status : 0;
    const url = err instanceof HttpErrorResponse ? err.url || '' : '';
    if (url.includes('/api/system/update-source')) {
      return 'This miner has no update server path.';
    }
    if (status === 404) {
      return 'The Satoshiware OS server folder does not have version.txt.';
    }
    if (status === 403 || status === 429) {
      return 'The Satoshiware OS server refused the check. Try again in a minute.';
    }
    return 'Could not reach the Satoshiware OS server.';
  }

  private httpMessage(err: unknown, fallback: string): string {
    if (err instanceof HttpErrorResponse) {
      const body = typeof err.error === 'string' ? err.error : err.error?.message;
      return body || err.message || fallback;
    }
    return fallback;
  }
}
