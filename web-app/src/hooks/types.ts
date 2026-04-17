export type EmulatorStatus =
  | 'idle'
  | 'loading'
  | 'ready'
  | 'running'
  | 'error';

export interface EmulatorState {
  status: EmulatorStatus;
  errorMessage: string | null;
  fps: number;
}

export interface EmulatorActions {
  loadRom: (file: File) => Promise<void>;
  loadRomFromUrl: (url: string, filename: string) => Promise<void>;
  start: () => void;
  pause: () => void;
  restart: () => void;
}

export interface UseEmulatorReturn extends EmulatorState, EmulatorActions {
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  containerRef: React.RefObject<HTMLDivElement | null>;
}
