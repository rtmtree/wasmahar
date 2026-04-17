import { useRef, useState, useCallback, useEffect } from 'react';
import type { UseEmulatorReturn, EmulatorStatus } from './types';

// Emscripten module interface
interface EmscriptenModule {
  canvas: HTMLCanvasElement;
  callMain: (args?: string[]) => void;
  print: (text: string) => void;
  printErr: (text: string) => void;
  onAbort: (what: unknown) => void;
  locateFile: (filename: string) => string;
  // Emscripten runtime methods (exported via EXPORTED_RUNTIME_METHODS)
  cwrap: (name: string, returnType: string, argTypes: string[]) => (...args: unknown[]) => unknown;
  ccall: (name: string, returnType: string, argTypes: string[], args: unknown[]) => unknown;
  FS: {
    writeFile: (path: string, data: Uint8Array) => void;
    mkdir: (path: string) => void;
    readFile: (path: string) => Uint8Array;
  };
}

type EmscriptenFactory = (config: Partial<EmscriptenModule>) => Promise<EmscriptenModule>;

const WASM_BASE = '/wasm';
const ROM_PATH = '/rom.3ds';
const DEFAULT_ROM_URL = '/ab.3ds';

export function useEmulator(): UseEmulatorReturn {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const moduleRef = useRef<EmscriptenModule | null>(null);
  const animFrameRef = useRef<number>(0);
  const fpsCounterRef = useRef<{ frames: number; lastTime: number }>({ frames: 0, lastTime: 0 });

  const [status, setStatus] = useState<EmulatorStatus>('idle');
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [fps, setFps] = useState(0);

  // Load the WASM module script
  const loadWasmModule = useCallback(async (): Promise<EmscriptenFactory> => {
    return new Promise((resolve, reject) => {
      const globalWindow = window as unknown as Record<string, unknown>;
      if (globalWindow['createAzaharModule']) {
        resolve(globalWindow['createAzaharModule'] as EmscriptenFactory);
        return;
      }

      const script = document.createElement('script');
      script.src = `${WASM_BASE}/azahar.js`;
      script.onload = () => {
        const factory = globalWindow['createAzaharModule'];
        if (factory) {
          resolve(factory as EmscriptenFactory);
        } else {
          reject(new Error('WASM module factory not found after loading script'));
        }
      };
      script.onerror = () => reject(new Error('Failed to load WASM module script'));
      document.head.appendChild(script);
    });
  }, []);

  // FPS counter loop
  const startFpsCounter = useCallback(() => {
    const tick = () => {
      const counter = fpsCounterRef.current;
      const now = performance.now();
      counter.frames++;
      const elapsed = now - counter.lastTime;
      if (elapsed >= 1000) {
        setFps(Math.round((counter.frames * 1000) / elapsed));
        counter.frames = 0;
        counter.lastTime = now;
      }
      animFrameRef.current = requestAnimationFrame(tick);
    };
    fpsCounterRef.current = { frames: 0, lastTime: performance.now() };
    animFrameRef.current = requestAnimationFrame(tick);
  }, []);

  const stopFpsCounter = useCallback(() => {
    if (animFrameRef.current) {
      cancelAnimationFrame(animFrameRef.current);
      animFrameRef.current = 0;
    }
    setFps(0);
  }, []);

  const loadRom = useCallback(async (file: File) => {
    try {
      setStatus('loading');
      setErrorMessage(null);

      if (!canvasRef.current) {
        throw new Error('Canvas element not ready');
      }

      // The Azahar WASM is built with pthreads, which requires SharedArrayBuffer
      // and a cross-origin-isolated context. Fail fast with a clear message
      // rather than hanging forever on "Loading emulator…" in browsers that
      // don't expose it (e.g. embedded Electron previews with SAB disabled).
      if (typeof SharedArrayBuffer === 'undefined' || !self.crossOriginIsolated) {
        throw new Error(
          'This browser does not support SharedArrayBuffer in a cross-origin-' +
          'isolated context, which the threaded WASM emulator requires. ' +
          'Open http://localhost:5180/ in regular Chrome/Firefox to run the ROM.',
        );
      }

      // Read ROM file into a Uint8Array
      const romData = new Uint8Array(await file.arrayBuffer());

      // Load the WASM module
      const factory = await loadWasmModule();

      const module = await factory({
        canvas: canvasRef.current,
        print: (text: string) => {
          console.log('[Azahar]', text);
        },
        printErr: (text: string) => {
          console.error('[Azahar]', text);
        },
        onAbort: (what: unknown) => {
          console.error('[Azahar] Aborted:', what);
          // Capture stack trace at abort time for debugging
          console.error('[Azahar] Abort stack:', new Error().stack);
          setStatus('error');
          setErrorMessage(`Emulator aborted: ${what}`);
        },
        locateFile: (filename: string) => `${WASM_BASE}/${filename}`,
      });

      moduleRef.current = module;

      // Write ROM to the Emscripten virtual filesystem.
      // The factory promise resolves after the runtime is initialized
      // but before main() runs (since we haven't called callMain yet).
      console.log(`[Azahar] Writing ROM to virtual FS: ${ROM_PATH} (${romData.length} bytes)`);
      try {
        module.FS.writeFile(ROM_PATH, romData);
        console.log('[Azahar] ROM written to virtual FS successfully');
      } catch (fsErr) {
        console.error('[Azahar] Failed to write ROM to virtual FS:', fsErr);
        throw new Error(`Failed to write ROM to virtual filesystem: ${fsErr}`);
      }

      // Tell the C++ code where the ROM is located
      try {
        module.ccall('SetEmcRomPath', null, ['string'], [ROM_PATH]);
        console.log('[Azahar] ROM path set to:', ROM_PATH);
      } catch (ccallErr) {
        console.error('[Azahar] Failed to call SetEmcRomPath:', ccallErr);
        throw new Error(`Failed to set ROM path: ${ccallErr}`);
      }

      // Now call main() which will call LaunchEmcFrontend()
      // The ROM is already in the virtual FS and the path is set
      console.log('[Azahar] Starting emulator main()...');
      module.callMain([]);
      setStatus('running');
      startFpsCounter();
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Unknown error loading ROM';
      setErrorMessage(msg);
      setStatus('error');
    }
  }, [loadWasmModule, startFpsCounter]);

  const start = useCallback(() => {
    // With the new flow, starting is handled in loadRom directly
  }, []);

  const pause = useCallback(() => {
    setStatus('ready');
    stopFpsCounter();
  }, [stopFpsCounter]);

  const restart = useCallback(() => {
    stopFpsCounter();
    if (moduleRef.current) {
      try {
        moduleRef.current.callMain([]);
        setStatus('running');
        startFpsCounter();
      } catch (err) {
        const msg = err instanceof Error ? err.message : 'Failed to restart';
        setErrorMessage(msg);
        setStatus('error');
      }
    }
  }, [startFpsCounter, stopFpsCounter]);

  // Load ROM from a URL (fetches, converts to File-like blob, then loads)
  const loadRomFromUrl = useCallback(async (url: string, filename: string) => {
    try {
      setStatus('loading');
      setErrorMessage(null);
      console.log(`[Azahar] Fetching ROM from ${url}...`);
      const response = await fetch(url);
      if (!response.ok) {
        throw new Error(`Failed to fetch ROM: ${response.status} ${response.statusText}`);
      }
      const blob = await response.blob();
      const file = new File([blob], filename, { type: 'application/octet-stream' });
      await loadRom(file);
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Unknown error fetching ROM';
      setErrorMessage(msg);
      setStatus('error');
    }
  }, [loadRom]);

  // Auto-load default ROM on mount
  useEffect(() => {
    loadRomFromUrl(DEFAULT_ROM_URL, 'ab.3ds');
  }, [loadRomFromUrl]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopFpsCounter();
    };
  }, [stopFpsCounter]);

  // Wire canvas pointer events → emulator touch input via direct ccall
  // (bypasses SDL's multi-window mouse-focus routing in Emscripten).
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const touchState = { active: false, pointerId: -1 };

    const toCanvasPixels = (ev: PointerEvent) => {
      const r = canvas.getBoundingClientRect();
      // Translate CSS pixels to the canvas's drawing-buffer pixel space.
      const sx = canvas.width / r.width;
      const sy = canvas.height / r.height;
      return {
        x: Math.round((ev.clientX - r.left) * sx),
        y: Math.round((ev.clientY - r.top) * sy),
      };
    };

    const callTouch = (name: string, args?: [number, number]) => {
      const mod = moduleRef.current;
      if (!mod) return;
      try {
        if (args) {
          mod.ccall(name, null, ['number', 'number'], args);
        } else {
          mod.ccall(name, null, [], []);
        }
      } catch {
        // ignore — likely called before ccall is ready
      }
    };

    const onDown = (ev: PointerEvent) => {
      if (ev.button !== 0 && ev.pointerType === 'mouse') return;
      touchState.active = true;
      touchState.pointerId = ev.pointerId;
      canvas.setPointerCapture?.(ev.pointerId);
      const { x, y } = toCanvasPixels(ev);
      callTouch('EmcTouchDown', [x, y]);
      ev.preventDefault();
    };
    const onMove = (ev: PointerEvent) => {
      if (!touchState.active || ev.pointerId !== touchState.pointerId) return;
      const { x, y } = toCanvasPixels(ev);
      callTouch('EmcTouchMove', [x, y]);
    };
    const onUp = (ev: PointerEvent) => {
      if (ev.pointerId !== touchState.pointerId) return;
      touchState.active = false;
      try { canvas.releasePointerCapture?.(ev.pointerId); } catch { /* empty */ }
      callTouch('EmcTouchUp');
    };

    canvas.addEventListener('pointerdown', onDown);
    canvas.addEventListener('pointermove', onMove);
    canvas.addEventListener('pointerup', onUp);
    canvas.addEventListener('pointercancel', onUp);
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    return () => {
      canvas.removeEventListener('pointerdown', onDown);
      canvas.removeEventListener('pointermove', onMove);
      canvas.removeEventListener('pointerup', onUp);
      canvas.removeEventListener('pointercancel', onUp);
    };
  }, []);

  return {
    status,
    errorMessage,
    fps,
    canvasRef,
    containerRef,
    loadRom,
    loadRomFromUrl,
    start,
    pause,
    restart,
  };
}
