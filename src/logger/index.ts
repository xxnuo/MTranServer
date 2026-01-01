import { getConfig } from '../config/index.js';

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

let currentLogLevel: LogLevel = 'warn';

export function setLogLevel(level: LogLevel) {
  currentLogLevel = level;
}

export function getLogLevel(): string {
  return currentLogLevel;
}

function shouldLog(level: LogLevel): boolean {
  const config = getConfig();
  const configLevel = (config.logLevel as LogLevel) || 'warn';
  return logLevels[level] >= logLevels[configLevel];
}

function formatMessage(level: string, color: string, message: string, ...args: any[]): string {
  const timestamp = new Date().toISOString().replace('T', ' ').replace('Z', '');
  const formatted = args.length > 0
    ? message.replace(/%[sdv]/g, () => String(args.shift() ?? ''))
    : message;
  return `${color}[${level.toUpperCase()}]${colors.reset} ${timestamp} ${formatted}`;
}

export function debug(message: string, ...args: any[]) {
  if (shouldLog('debug')) {
    console.log(formatMessage('debug', colors.cyan, message, ...args));
  }
}

export function info(message: string, ...args: any[]) {
  if (shouldLog('info')) {
    console.log(formatMessage('info', colors.green, message, ...args));
  }
}

export function warn(message: string, ...args: any[]) {
  if (shouldLog('warn')) {
    console.warn(formatMessage('warn', colors.yellow, message, ...args));
  }
}

export function error(message: string, ...args: any[]) {
  if (shouldLog('error')) {
    console.error(formatMessage('error', colors.red, message, ...args));
  }
}

export function fatal(message: string, ...args: any[]) {
  console.error(formatMessage('error', colors.red, message, ...args));
  process.exit(1);
}

export default {
  setLogLevel,
  getLogLevel,
  debug,
  info,
  warn,
  error,
  fatal,
};
