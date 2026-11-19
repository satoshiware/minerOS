import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';
import { map, switchMap } from 'rxjs/operators';
import { PublishedFirmware, firmwareFolderUrl, publishedFirmware } from './firmware-update-source';

interface UpdateSourceResponse {
  url?: string;
}

@Injectable({
  providedIn: 'root'
})
export class FirmwareUpdateService {

  constructor(
    private httpClient: HttpClient
  ) { }

  /** Read version.txt from the folder path stored on the miner. */
  public check(): Observable<PublishedFirmware> {
    return this.httpClient.get<UpdateSourceResponse>('/api/system/update-source').pipe(
      switchMap((source) => {
        const folder = firmwareFolderUrl(source?.url ?? '');
        return this.httpClient.get(`${folder}/version.txt`, { responseType: 'text' }).pipe(
          map((versionText) => publishedFirmware(folder, versionText))
        );
      })
    );
  }

  public download(url: string): Observable<Blob> {
    return this.httpClient.get(url, { responseType: 'blob' });
  }
}
