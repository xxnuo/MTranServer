import fs from 'fs';
import path from 'path';
import util from 'util';
import { getConfig } from '@/config/index.js';

type LogLevel = 'debug' | 'info' | 'warn' | 'error';

const logLevels: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
};

const colors = {
  reset: '\x1b[0m',
  cyan: '\x1b[36m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
};

let currentLogLevel: LogLevel | null = null;

// File logging state
let logStream: fs.WriteStream | null = null;
let currentLogDate = '';

export function setLogLevel(level: LogLevel) {
  currentLogLevel = level;
}

export function getLogLevel(): string {
  if (currentLogLevel === null) {
    const config = getConfig();
    currentLogLevel = (config.logLevel as LogLevel) || 'warn';
  }
  return currentLogLevel;
}

function shouldLog(level: LogLevel): boolean {
  const currentLevel = getLogLevel() as LogLevel;
  return logLevels[level] >= logLevels[currentLevel];
}

function getTimestamp(): string {
  return new Date().toISOString().replace('T', ' ').replace('Z', '');
}

function getLogStream() {
  const config = getConfig();
  if (!config.logToFile) return null;

  const today = new Date().toISOString().split('T')[0];
  if (currentLogDate !== today || !logStream) {
    if (logStream) {
      logStream.end();
    }
    if (!fs.existsSync(config.logDir)) {
      try {
        fs.mkdirSync(config.logDir, { recursive: true });
      } catch (err) {
        console.error(`Failed to create log directory: ${config.logDir}`, err);
        return null;
      }
    }
    const logPath = path.join(config.logDir, `mtran-${today}.log`);
    try {
      logStream = fs.createWriteStream(logPath, { flags: 'a' });
      currentLogDate = today;
    } catch (err) {
      console.error(`Failed to create log stream: ${logPath}`, err);
      return null;
    }
  }
  return logStream;
}

function logInternal(level: LogLevel, color: string, force: boolean, message: string, ...args: any[]) {
  if (!force && !shouldLog(level)) return;

  const timestamp = getTimestamp();
  const formattedMessage = util.format(message, ...args);
  
  const config = getConfig();

  // Console output (with colors)
  if (config.logConsole) {
    const consoleOutput = `${color}[${level.toUpperCase()}]${colors.reset} ${timestamp} ${formattedMessage}`;
    if (level === 'error' || level === 'warn') {
      console.error(consoleOutput);
    } else {
      console.log(consoleOutput);
    }
  }

  // File output (without colors)
  const stream = getLogStream();
  if (stream) {
    const fileOutput = `[${level.toUpperCase()}] ${timestamp} ${formattedMessage}\n`;
    stream.write(fileOutput);
  }
}

function log(level: LogLevel, color: string, message: string, ...args: any[]) {
  logInternal(level, color, false, message, ...args);
}

export function debug(message: string, ...args: any[]) {
  log('debug', colors.cyan, message, ...args);
}

export function info(message: string, ...args: any[]) {
  log('info', colors.green, message, ...args);
}

export function important(message: string, ...args: any[]) {
  logInternal('info', colors.green, true, message, ...args);
}

export function warn(message: string, ...args: any[]) {
  log('warn', colors.yellow, message, ...args);
}

export function error(message: string, ...args: any[]) {
  log('error', colors.red, message, ...args);
}

export function fatal(message: string, ...args: any[]) {
  log('error', colors.red, message, ...args);
  process.exit(1);
}

export default {
  setLogLevel,
  getLogLevel,
  debug,
  info,
  important,
  warn,
  error,
  fatal,
};
