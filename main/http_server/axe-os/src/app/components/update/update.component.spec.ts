import { ComponentFixture, TestBed } from '@angular/core/testing';

import { UpdateComponent } from './update.component';
import { ModalComponent } from '../modal/modal.component';
import { ButtonModule } from 'primeng/button';
import { InputTextModule } from 'primeng/inputtext';
import { ProgressBarModule } from 'primeng/progressbar';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

describe('UpdateComponent', () => {
  let component: UpdateComponent;
  let fixture: ComponentFixture<UpdateComponent>;

  beforeEach(() => {
    localStorage.clear();
    TestBed.configureTestingModule({
      declarations: [UpdateComponent, ModalComponent],
      imports: [ButtonModule, InputTextModule, ProgressBarModule],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(UpdateComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create without a file picker', () => {
    expect(component).toBeTruthy();
    expect(fixture.nativeElement.textContent).toContain('Check for update');
    expect(fixture.nativeElement.textContent).not.toContain('Browse');
    expect(fixture.nativeElement.querySelector('p-fileupload')).toBeNull();
  });

  it('does not save a source that is not a repository path', () => {
    component.editingSource = true;
    component.draft = { owner: 'satoshiware', repo: 'minerOS', branch: 'main', folder: '../firmware' };
    component.saveSource();
    expect(component.source.folder).toBe('firmware');
    expect(component.sourceError).toContain('Folder');
  });
});
