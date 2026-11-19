import { Component, ViewChild } from '@angular/core';
import { Observable, TimeoutError, take, timeout } from 'rxjs';
import { HttpErrorResponse, HttpEventType } from '@angular/common/http';
import { ToastrService } from 'ngx-toastr';
import { GithubUpdateService } from 'src/app/services/github-update.service';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { LocalStorageService } from 'src/app/local-storage.service';
import { ModalComponent } from '../modal/modal.component';
import { SystemInfo } from 'src/app/generated/models';
import {
  DEFAULT_FIRMWARE_UPDATE_SOURCE,
  FIRMWARE_UPDATE_SOURCE_KEY,
  FirmwareUpdateSource,
  PublishedFirmware,
  firmwareUpdateSourceError,
  normalizeFirmwareUpdateSource,
} from 'src/app/services/firmware-update-source';

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

  public source: FirmwareUpdateSource = { ...DEFAULT_FIRMWARE_UPDATE_SOURCE };
  public draft: FirmwareUpdateSource = { ...DEFAULT_FIRMWARE_UPDATE_SOURCE };
  public editingSource = false;
  public sourceError = '';

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
    private githubUpdateService: GithubUpdateService,
    private localStorageService: LocalStorageService,
  ) {
    this.info$ = this.liveDataService.info$;
    this.source = normalizeFirmwareUpdateSource(
      this.localStorageService.getObject(FIRMWARE_UPDATE_SOURCE_KEY)
    );
    this.draft = { ...this.source };
  }

  public get sourceLabel(): string {
    return `${this.source.owner}/${this.source.repo} · ${this.source.branch} · ${this.source.folder}/`;
  }

  public get usingDefaultSource(): boolean {
    return this.source.owner === DEFAULT_FIRMWARE_UPDATE_SOURCE.owner
      && this.source.repo === DEFAULT_FIRMWARE_UPDATE_SOURCE.repo
      && this.source.branch === DEFAULT_FIRMWARE_UPDATE_SOURCE.branch
      && this.source.folder === DEFAULT_FIRMWARE_UPDATE_SOURCE.folder;
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
        this.githubUpdateService.check(this.source).subscribe({
          next: (published) => {
            this.published = published;
            if (published.version === this.deviceVersion) {
              this.checkStatus = 'current';
              this.checkMessage = `Published version ${published.version} matches this miner.`;
              return;
            }
            this.checkStatus = 'available';
            this.checkMessage = `Published version ${published.version} is in that folder. This miner is ${this.deviceVersion || 'unknown'}.`;
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

    this.githubUpdateService.download(published.www.downloadUrl).subscribe({
      next: (www) => this.sendWebPage(www, published),
      error: () => this.failUpdate('Could not download www.bin from that folder.'),
    });
  }

  public saveSource(): void {
    const next: FirmwareUpdateSource = {
      owner: this.draft.owner.trim(),
      repo: this.draft.repo.trim(),
      branch: this.draft.branch.trim(),
      folder: this.draft.folder.trim(),
    };
    const error = firmwareUpdateSourceError(next);
    this.sourceError = error || '';
    if (error) {
      return;
    }
    this.source = next;
    this.draft = { ...next };
    this.localStorageService.setObject(FIRMWARE_UPDATE_SOURCE_KEY, next);
    this.editingSource = false;
    this.resetCheck();
  }

  public resetSource(): void {
    this.localStorageService.removeItem(FIRMWARE_UPDATE_SOURCE_KEY);
    this.source = { ...DEFAULT_FIRMWARE_UPDATE_SOURCE };
    this.draft = { ...this.source };
    this.sourceError = '';
    this.editingSource = false;
    this.resetCheck();
  }

  public formatSize(bytes: number): string {
    if (bytes < 1024) {
      return `${bytes} B`;
    }
    const kb = bytes / 1024;
    if (kb < 1024) {
      return `${kb.toFixed(0)} KB`;
    }
    return `${(kb / 1024).toFixed(1)} MB`;
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
    this.githubUpdateService.download(published.espMiner.downloadUrl).subscribe({
      next: (firmware) => this.sendFirmware(firmware),
      error: () => this.failUpdate('The web page was updated. esp-miner.bin could not be downloaded.'),
    });
  }

  private sendFirmware(file: Blob): void {
    this.systemService.performOTAUpdate(file).subscribe({
      next: (event) => {
        if (event.type === HttpEventType.UploadProgress && event.total) {
          this.firmwareUpdateProgress = Math.round((event.loaded / event.total) * 100);
        } else if (event.type === HttpEventType.Response && event.ok) {
          this.updateStatus = 'success';
          this.updateMessage = 'Published firmware installed. The miner is restarting. Hard-refresh this page once it is back.';
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

  private resetCheck(): void {
    this.checkStatus = 'idle';
    this.checkMessage = '';
    this.published = null;
    this.deviceVersion = '';
  }

  private explainCheckError(err: unknown): string {
    const status = err instanceof HttpErrorResponse ? err.status : 0;
    if (status === 404) {
      return `Nothing at ${this.sourceLabel} Make sure that branch has version.txt plus esp-miner.bin and www.bin in ${this.source.folder}/, and that the repository is public.`;
    }
    if (status === 403 || status === 429) {
      return 'GitHub refused the check. Try again in a minute.';
    }
    if (err instanceof Error && err.message.includes('esp-miner.bin')) {
      return err.message;
    }
    return `Could not read ${this.sourceLabel}`;
  }

  private httpMessage(err: unknown, fallback: string): string {
    if (err instanceof HttpErrorResponse) {
      const body = typeof err.error === 'string' ? err.error : err.error?.message;
      return body || err.message || fallback;
    }
    return fallback;
  }
}
