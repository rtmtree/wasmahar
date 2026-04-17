import type { EmulatorStatus } from '../hooks/types';

interface EmulatorCanvasProps {
  status: EmulatorStatus;
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  containerRef: React.RefObject<HTMLDivElement | null>;
}

export function EmulatorCanvas({ status, canvasRef, containerRef }: EmulatorCanvasProps) {
  const isLoading = status === 'loading';
  const isError = status === 'error';
  const isActive = status === 'running' || status === 'ready';

  return (
    <div ref={containerRef} className={`emulator-canvas-container ${isActive ? 'active' : ''}`}>
      <canvas
        ref={canvasRef}
        id="emulator-canvas"
        width={400}
        height={480}
      />
      {isLoading && (
        <div className="emulator-overlay">
          <div className="spinner" />
          <p>Loading emulator...</p>
        </div>
      )}
      {isError && (
        <div className="emulator-overlay error">
          <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
            <circle cx="12" cy="12" r="10" />
            <line x1="15" y1="9" x2="9" y2="15" />
            <line x1="9" y1="9" x2="15" y2="15" />
          </svg>
          <p>Failed to load</p>
        </div>
      )}
      {!isActive && !isLoading && !isError && (
        <div className="emulator-overlay idle">
          <p>Load a ROM to begin</p>
        </div>
      )}
    </div>
  );
}
