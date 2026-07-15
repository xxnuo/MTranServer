import { Request, Response, NextFunction } from 'express';
import { AsyncLocalStorage } from 'node:async_hooks';

const requestContext = new AsyncLocalStorage<AbortSignal>();

export function getRequestSignal(): AbortSignal | undefined {
  return requestContext.getStore();
}

export function requestCancellation() {
  return (req: Request, res: Response, next: NextFunction) => {
    const controller = new AbortController();
    const abort = () => {
      if (!controller.signal.aborted) {
        controller.abort();
      }
    };
    function cleanup() {
      req.removeListener('aborted', onReqAborted);
      res.removeListener('close', onResClose);
      res.removeListener('finish', cleanup);
    }
    function onReqAborted() {
      abort();
      cleanup();
    }
    function onResClose() {
      // Use writableEnded (res.end() was called) rather than writableFinished
      // (data fully flushed) to avoid a race where close fires between end()
      // and the finish event while data is still being written.
      if (!res.writableEnded) abort();
      cleanup();
    }

    req.once('aborted', onReqAborted);
    res.once('close', onResClose);
    res.once('finish', cleanup);

    requestContext.run(controller.signal, next);
  };
}
