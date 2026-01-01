const messages = {
  en: {
    trayOpenMain: 'Open Main Window',
    trayOpenBrowser: 'Open in Browser',
    trayOpenUi: 'Main UI',
    trayOpenDocs: 'API Docs',
    trayOpenRepo: 'Repository',
    trayServiceStatus: 'Service Status',
    trayServiceRunning: 'Running',
    trayServiceStopped: 'Stopped',
    trayServiceManagement: 'Service Management',
    trayOpenSettings: 'Settings',
    trayRestart: 'Restart',
    trayData: 'Data',
    trayOpenModels: 'Open Model Folder',
    trayOpenConfig: 'Open Config Folder',
    trayVersion: 'Version',
    trayQuit: 'Quit',
    appName: 'MTranServer',
    portInUseTitle: 'Port is in use',
    portInUseDetail: 'Port {port} is already in use. Close the process occupying it, or use a random port to start the server.',
    portInUseUseRandom: 'Use Random Port',
    portInUseQuit: 'Quit',
    serverStartFailed: 'Failed to start server',
    serverStartFailedDetail: 'Unable to start MTranServer. Please check configuration and try again.',
    serverRestartFailed: 'Failed to restart server'
  },
  zh: {
    trayOpenMain: '打开主界面',
    trayOpenBrowser: '浏览器打开',
    trayOpenUi: '主界面',
    trayOpenDocs: 'API 文档',
    trayOpenRepo: '仓库地址',
    trayServiceStatus: '服务状态',
    trayServiceRunning: '启动中',
    trayServiceStopped: '已停止',
    trayServiceManagement: '服务管理',
    trayOpenSettings: '设置',
    trayRestart: '重启',
    trayData: '数据',
    trayOpenModels: '打开模型目录',
    trayOpenConfig: '打开配置目录',
    trayVersion: '版本号',
    trayQuit: '退出程序',
    appName: 'MTranServer',
    portInUseTitle: '端口被占用',
    portInUseDetail: '端口 {port} 已被占用，请关闭占用该端口的进程，或使用随机端口启动服务。',
    portInUseUseRandom: '使用随机端口',
    portInUseQuit: '退出',
    serverStartFailed: '服务启动失败',
    serverStartFailedDetail: '无法启动 MTranServer，请检查配置后重试。',
    serverRestartFailed: '服务重启失败'
  },
  ja: {
    trayOpenMain: 'メイン画面を開く',
    trayOpenBrowser: 'ブラウザで開く',
    trayOpenUi: 'メイン画面',
    trayOpenDocs: 'API ドキュメント',
    trayOpenRepo: 'リポジトリ',
    trayServiceStatus: 'サービス状態',
    trayServiceRunning: '稼働中',
    trayServiceStopped: '停止中',
    trayServiceManagement: 'サービス管理',
    trayOpenSettings: '設定',
    trayRestart: '再起動',
    trayData: 'データ',
    trayOpenModels: 'モデルフォルダを開く',
    trayOpenConfig: '設定フォルダを開く',
    trayVersion: 'バージョン',
    trayQuit: '終了',
    appName: 'MTranServer',
    portInUseTitle: 'ポートが使用中です',
    portInUseDetail: 'ポート {port} は使用中です。使用中のプロセスを終了するか、ランダムポートで起動してください。',
    portInUseUseRandom: 'ランダムポートを使用',
    portInUseQuit: '終了',
    serverStartFailed: 'サーバーの起動に失敗しました',
    serverStartFailedDetail: 'MTranServer を起動できません。設定を確認してください。',
    serverRestartFailed: 'サーバーの再起動に失敗しました'
  }
};

export function resolveLocale(configLocale, appLocale) {
  if (configLocale && configLocale !== 'system') {
    return messages[configLocale] ? configLocale : 'en';
  }
  const lower = (appLocale || 'en').toLowerCase();
  if (lower.startsWith('zh')) return 'zh';
  if (lower.startsWith('ja')) return 'ja';
  return 'en';
}

export function getMessages(locale) {
  return messages[locale] || messages.en;
}
