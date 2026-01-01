import { Controller, Get, Route, Tags, SuccessResponse } from 'tsoa';

interface VersionResponse {
  version: string;
}

interface HealthResponse {
  status: string;
}

@Route('')
@Tags('System')
export class SystemController extends Controller {
  @Get('version')
  @SuccessResponse('200', 'Success')
  public async getVersion(): Promise<VersionResponse> {
    const { getVersion } = await import('@/version/index.js');
    return { version: getVersion() };
  }

  @Get('health')
  @SuccessResponse('200', 'Success')
  public async getHealth(): Promise<HealthResponse> {
    return { status: 'ok' };
  }

  @Get('__heartbeat__')
  @SuccessResponse('200', 'Success')
  public async heartbeat(): Promise<void> {
    this.setStatus(200);
  }

  @Get('__lbheartbeat__')
  @SuccessResponse('200', 'Success')
  public async lbHeartbeat(): Promise<void> {
    this.setStatus(200);
  }
}
