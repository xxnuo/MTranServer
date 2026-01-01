import { app, BrowserWindow, Tray, Menu, nativeImage } from 'electron';
import path from 'path';
import { fileURLToPath } from 'url';

// --- ESM 兼容性处理 ---
// 在 ESM 模式下无法直接使用 __dirname，需要通过 import.meta.url 手动生成
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// --- 1. 引入你的 Express 入口文件 ---
// 使用 import 语法直接导入，这会执行 ./dist/main.js 中的代码（启动服务器）
import './dist/main.js';

const PORT = 8989;

let mainWindow;
let tray;

// 防止应用多开
const gotTheLock = app.requestSingleInstanceLock();

if (!gotTheLock) {
    app.quit();
} else {
    app.on('second-instance', () => {
        // 如果用户尝试打开第二个实例，则聚焦当前窗口
        if (mainWindow) {
            if (mainWindow.isMinimized()) mainWindow.restore();
            if (!mainWindow.isVisible()) mainWindow.show();
            mainWindow.focus();
        }
    });

    app.whenReady().then(() => {
        createWindow();
        createTray();
    });
}

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1024,
        height: 768,
        show: false, // 启动时不直接显示窗口
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true
        }
    });

    // 加载 Express 服务地址
    mainWindow.loadURL(`http://localhost:${PORT}`);

    // 监听关闭事件：点击 X 不退出，而是隐藏到托盘
    mainWindow.on('close', (event) => {
        if (!app.isQuitting) {
            event.preventDefault();
            mainWindow.hide();
            return false;
        }
    });
}

function createTray() {
    // 2. 准备图标
    // 确保你的路径下有 images/icon.png
    const iconPath = path.join(__dirname, "images", "icons", 'icon@16px.png');

    // 增加一个容错，防止图标路径不对导致报错
    let trayIcon;
    try {
        trayIcon = nativeImage.createFromPath(iconPath);
        if (trayIcon.isEmpty()) {
            console.warn('警告: 托盘图标为空，请检查路径:', iconPath);
        }
    } catch (e) {
        console.error('加载图标失败:', e);
        trayIcon = nativeImage.createEmpty();
    }

    tray = new Tray(trayIcon);
    tray.setToolTip('我的 Express 项目');

    // 3. 定义托盘右键菜单
    const contextMenu = Menu.buildFromTemplate([
        {
            label: '显示主界面',
            click: () => mainWindow.show()
        },
        {
            label: '重启页面', // 注意：这里只是刷新页面，不是重启后台服务
            click: () => {
                mainWindow.reload();
            }
        },
        { type: 'separator' },
        {
            label: '退出',
            click: () => {
                app.isQuitting = true;
                app.quit();
            }
        }
    ]);

    tray.setContextMenu(contextMenu);

    // 点击托盘图标切换显示/隐藏
    tray.on('click', () => {
        if (mainWindow.isVisible()) {
            mainWindow.hide();
        } else {
            mainWindow.show();
        }
    });
}

// macOS 特殊处理
app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    } else {
        mainWindow.show();
    }
});