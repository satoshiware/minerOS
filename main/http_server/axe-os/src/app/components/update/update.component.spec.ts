import { ComponentFixture, TestBed } from '@angular/core/testing';

import { UpdateComponent } from './update.component';
import { ModalComponent } from '../modal/modal.component';
import { ButtonModule } from 'primeng/button';
import { ProgressBarModule } from 'primeng/progressbar';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

describe('UpdateComponent', () => {
  let component: UpdateComponent;
  let fixture: ComponentFixture<UpdateComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [UpdateComponent, ModalComponent],
      imports: [ButtonModule, ProgressBarModule],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(UpdateComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should offer a check and no source editor', () => {
    const text = fixture.nativeElement.textContent;
    expect(component).toBeTruthy();
    expect(text).toContain('Check for update');
    expect(text).toContain('Satoshiware OS server');
    expect(text).not.toContain('Change update source');
    expect(text).not.toContain('GitHub');
    expect(text).not.toContain('Browse');
    expect(fixture.nativeElement.querySelector('p-fileupload')).toBeNull();
  });
});
