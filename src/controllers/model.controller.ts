import {
  Body,
  Controller,
  Delete,
  Get,
  Path,
  Post,
  Query,
  Route,
  Security,
  SuccessResponse,
  Tags,
} from 'tsoa';
import {
  getDownloadJob,
  getDownloadJobs,
  getOcrModelCatalog,
  startOcrModelDownload,
  getModelCatalog,
  getModelDetails,
  getModelDownloadSettings,
  cancelModelDownload,
  testDownloadSources,
  testDownloadLatencies,
  refreshModelRecords,
  removeModel,
  startModelDownload,
  updateModelDownloadSettings,
} from '@/models/manager.js';

interface ModelDownloadRequest {
  from: string;
  to: string;
  version?: string;
  architecture?: string;
}

interface ModelFileState {
  fileType: string;
  filename: string;
  compressedSize: number;
  decompressedSize: number;
  installedSize: number;
  installed: boolean;
}

interface ManagedModel {
  id: string;
  from: string;
  to: string;
  architecture: string;
  version: string;
  status: string;
  downloadSize: number;
  installedSize: number;
  progress: number;
  downloadId?: string;
  error?: string;
  files: ModelFileState[];
}

interface ModelCatalog {
  modelDir: string;
  recordsLoadedAt: string | null;
  models: ManagedModel[];
  totalModels: number;
  filteredModels: number;
  page: number;
  pageSize: number;
  totalPages: number;
  architectures: string[];
  statusCounts: Record<string, number>;
}

interface OcrModelFile {
  role: string;
  path: string;
  url: string;
  sizeBytes: number;
  sha256: string;
  available: boolean;
}

interface OcrModel {
  id: string;
  name: string;
  version: string;
  variant: string;
  backend: string;
  device: string;
  languages: string[];
  recommended: boolean;
  description: Record<string, string>;
  sizeBytes: number;
  available: boolean;
  files: OcrModelFile[];
  status: string;
  progress: number;
  downloadedBytes: number;
  downloadId?: string;
  error?: string;
  errorCode?: string;
}

interface OcrModelCatalog {
  schema: number;
  updatedAt: string;
  models: OcrModel[];
}

interface DownloadJob {
  kind: 'translation' | 'ocr';
  id: string;
  from: string;
  to: string;
  architecture: string;
  version: string;
  status: string;
  progress: number;
  downloadedBytes: number;
  totalBytes: number;
  currentFile?: string;
  error?: string;
  createdAt: string;
  startedAt?: string;
  finishedAt?: string;
  modelId?: string;
}

interface ModelDownloadResponse extends DownloadJob {}

interface OcrModelDownloadRequest {
  modelId: string;
}

interface ModelRemoveResponse {
  removed: boolean;
}

interface ModelDownloadSettingsRequest {
  source: 'mirror' | 'official';
  mirrorUrl: string;
  proxyUrl: string;
}

interface DownloadSpeedTestRequest {
  mirrorUrl?: string;
  proxyUrl?: string;
}

@Route('models')
@Tags('Models')
@Security('api_token')
export class ModelController extends Controller {
  @Get('/')
  @SuccessResponse('200', 'Success')
  public async listModels(
    @Query() page?: number,
    @Query() pageSize?: number,
    @Query() query?: string,
    @Query() status?: string,
    @Query() architecture?: string,
    @Query() locale?: string,
  ): Promise<ModelCatalog> {
    return getModelCatalog({ page, pageSize, query, status, architecture, locale });
  }

  @Get('ocr')
  @SuccessResponse('200', 'Success')
  public async listOcrModels(): Promise<OcrModelCatalog> {
    return getOcrModelCatalog();
  }

  @Post('ocr/download')
  @SuccessResponse('202', 'Accepted')
  public async downloadOcrModel(
    @Body() body: OcrModelDownloadRequest
  ): Promise<ModelDownloadResponse> {
    const job = await startOcrModelDownload(body.modelId);
    this.setStatus(202);
    return job;
  }

  @Get('settings')
  @SuccessResponse('200', 'Success')
  public async getDownloadSettings() {
    return getModelDownloadSettings();
  }

  @Post('settings')
  @SuccessResponse('200', 'Success')
  public async updateDownloadSettings(
    @Body() body: ModelDownloadSettingsRequest
  ) {
    return updateModelDownloadSettings(body);
  }

  @Get('speedtest')
  @SuccessResponse('200', 'Success')
  public async speedTest() {
    return testDownloadSources();
  }

  @Post('speedtest')
  @SuccessResponse('200', 'Success')
  public async speedTestWithSettings(
    @Body() body: DownloadSpeedTestRequest
  ) {
    return testDownloadSources(body);
  }

  @Get('latency')
  @SuccessResponse('200', 'Success')
  public async latencyTest() {
    return testDownloadLatencies();
  }

  @Post('latency')
  @SuccessResponse('200', 'Success')
  public async latencyTestWithSettings(
    @Body() body: DownloadSpeedTestRequest
  ) {
    return testDownloadLatencies(body);
  }

  @Get('pair/{from}/{to}')
  @SuccessResponse('200', 'Success')
  public async getPairModels(
    @Path() from: string,
    @Path() to: string
  ): Promise<ManagedModel[]> {
    return getModelDetails(from, to);
  }

  @Get('downloads')
  @SuccessResponse('200', 'Success')
  public async listDownloads(@Query() active?: boolean): Promise<DownloadJob[]> {
    return getDownloadJobs(active === true);
  }

  @Get('downloads/{id}')
  @SuccessResponse('200', 'Success')
  public async getDownload(@Path() id: string): Promise<DownloadJob> {
    return getDownloadJob(id);
  }

  @Delete('downloads/{id}')
  @SuccessResponse('200', 'Cancelled')
  public async cancelDownload(@Path() id: string): Promise<DownloadJob> {
    return cancelModelDownload(id);
  }

  @Post('download')
  @SuccessResponse('202', 'Accepted')
  public async downloadModel(
    @Body() body: ModelDownloadRequest
  ): Promise<ModelDownloadResponse> {
    const job = await startModelDownload(body.from, body.to, body.version, body.architecture);
    this.setStatus(202);
    return job;
  }

  @Post('refresh')
  @SuccessResponse('200', 'Success')
  public async refresh(): Promise<ModelCatalog> {
    return refreshModelRecords();
  }

  @Delete('{from}/{to}')
  @SuccessResponse('200', 'Success')
  public async remove(
    @Path() from: string,
    @Path() to: string
  ): Promise<ModelRemoveResponse> {
    const { cleanupAllEngines } = await import('@/services/index.js');
    cleanupAllEngines();
    await removeModel(from, to);
    return { removed: true };
  }
}
