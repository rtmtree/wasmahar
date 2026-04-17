import type { EmulatorStatus } from '../hooks/types';

interface ControlsProps {
  status: EmulatorStatus;
  fps: number;
  romName: string | null;
  onStart: () => void;
  onPause: () => void;
  onRestart: () => void;
  onUnload: () => void;
  onToggleFullscreen: () => void;
}

export function Controls({
  status,
  fps,
  romName,
  onStart,
  onPause,
  onRestart,
  onUnload,
  onToggleFullscreen,
}: ControlsProps) {
  const isRunning = status === 'running';
  const isReady = status === 'ready';
  const isActive = isRunning || isReady;

  return (
    <div className="controls-bar">
      <div className="controls-left">
        {romName && (
          <span className="rom-name" title={romName}>
            {romName}
          </span>
        )}
        {isActive && (
          <span className="fps-counter">{fps} FPS</span>
        )}
      </div>

      <div className="controls-center">
        {isReady && (
          <button className="btn btn-primary" onClick={onStart} title="Start">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
              <polygon points="5 3 19 12 5 21 5 3" />
            </svg>
          </button>
        )}
        {isRunning && (
          <button className="btn btn-warning" onClick={onPause} title="Pause">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
              <rect x="6" y="4" width="4" height="16" />
              <rect x="14" y="4" width="4" height="16" />
            </svg>
          </button>
        )}
        {isActive && (
          <button className="btn" onClick={onRestart} title="Restart">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="23 4 23 10 17 10" />
              <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10" />
            </svg>
          </button>
        )}
      </div>

      <div className="controls-right">
        {isActive && (
          <button className="btn" onClick={onToggleFullscreen} title="Fullscreen">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <polyline points="15 3 21 3 21 9" />
              <polyline points="9 21 3 21 3 15" />
              <line x1="21" y1="3" x2="14" y2="10" />
              <line x1="3" y1="21" x2="10" y2="14" />
            </svg>
          </button>
        )}
        {isActive && (
          <button className="btn btn-danger" onClick={onUnload} title="Unload ROM">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <line x1="18" y1="6" x2="6" y2="18" />
              <line x1="6" y1="6" x2="18" y2="18" />
            </svg>
          </button>
        )}
      </div>
    </div>
  );
}
