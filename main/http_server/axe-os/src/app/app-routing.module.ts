import { NgModule } from '@angular/core';
import { RouterModule, Routes } from '@angular/router';

import { HomeComponent } from './components/home/home.component';
import { LogsComponent } from './components/logs/logs.component';
import { SystemComponent } from './components/system/system.component';
import { UpdateComponent } from './components/update/update.component';
import { SettingsComponent } from './components/settings/settings.component';
import { NetworkComponent } from './components/network/network.component';
import { SwarmComponent } from './components/swarm/swarm.component';
import { ScoreboardComponent } from './components/scoreboard/scoreboard.component';
import { DesignComponent } from './components/design/design.component';
import { PoolComponent } from './components/pool/pool.component';
import { AppLayoutComponent } from './layout/app.layout.component';
import { ApModeGuard } from './guards/ap-mode.guard';

const PAGE_TITLE = 'SWare Miner Config';

const routes: Routes = [
  {
      path: 'ap',
      component: AppLayoutComponent,
      children: [
        {
          path: '',
          component: NetworkComponent,
          title: PAGE_TITLE,
        }
      ]
  },
  {
    path: '',
    component: AppLayoutComponent,
    canActivate: [ApModeGuard],
    children: [
      {
        path: '',
        component: HomeComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'logs',
        component: LogsComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'system',
        component: SystemComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'update',
        component: UpdateComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'network',
        component: NetworkComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'settings',
        component: SettingsComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'swarm',
        component: SwarmComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'scoreboard',
        component: ScoreboardComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'design',
        component: DesignComponent,
        title: PAGE_TITLE,
      },
      {
        path: 'pool',
        component: PoolComponent,
        title: PAGE_TITLE,
      }
    ]
  },

];

@NgModule({
  imports: [RouterModule.forRoot(routes)],
  exports: [RouterModule]
})
export class AppRoutingModule { }
