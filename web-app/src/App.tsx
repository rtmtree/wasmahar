import { useState, useCallback } from 'react';
import { useEmulator } from './hooks/useEmulator';
import { FileDrop } from './components/FileDrop';
import { Controls } from './components/Controls';
import { EmulatorCanvas } from './components/EmulatorCanvas';
import { VirtualController } from './components/VirtualController';
import type { EmulatorStatus } from './hooks/types';
import './App.css';

function App() {
  const {
    status,
    errorMessage,
    fps,
    canvasRef,
    containerRef,
    loadRom,
    start,
    pause,
    restart,
  } = useEmulator();

  const [romName, setRomName] = useState<string | null>(null);
  const [prevStatus, setPrevStatus] = useState<EmulatorStatus>('idle');

  const handleFileSelected = useCallback(async (file: File) => {
    setRomName(file.name);
    await loadRom(file);
  }, [loadRom]);

  const handleUnload = useCallback(() => {
    setRomName(null);
    // Reload the page to reset the WASM module
    window.location.reload();
  }, []);

  const handleToggleFullscreen = useCallback(() => {
    if (containerRef.current) {
      if (document.fullscreenElement) {
        document.exitFullscreen();
      } else {
        containerRef.current.requestFullscreen();
      }
    }
  }, [containerRef]);

  // Track previous status for pause -> ready transition
  if (status !== prevStatus) {
    setPrevStatus(status);
  }

  const showDropZone = status === 'idle' || status === 'error';

  return (
    <div className="app">
      <header className="app-header">
        <div className="app-title">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="var(--accent)" strokeWidth="2">
            <rect x="2" y="6" width="20" height="12" rx="2" />
            <line x1="6" y1="12" x2="10" y2="12" />
            <line x1="8" y1="10" x2="8" y2="14" />
            <circle cx="16" cy="10" r="1" fill="var(--accent)" />
            <circle cx="18" cy="12" r="1" fill="var(--accent)" />
          </svg>
          <h1>Azahar</h1>
        </div>
        <span className="app-subtitle">3DS Emulator</span>
      </header>

      <Controls
        status={status}
        fps={fps}
        romName={romName}
        onStart={start}
        onPause={pause}
        onRestart={restart}
        onUnload={handleUnload}
        onToggleFullscreen={handleToggleFullscreen}
      />

      <main className="app-main">
        {showDropZone && (
          <div className="drop-zone-wrapper">
            <FileDrop onFileSelected={handleFileSelected} />
            {errorMessage && (
              <div className="error-message">
                {errorMessage}
              </div>
            )}
          </div>
        )}

        <EmulatorCanvas
          status={status}
          canvasRef={canvasRef}
          containerRef={containerRef}
        />

        {status !== 'idle' && status !== 'error' && <VirtualController />}
      </main>

      <footer className="app-footer">
        <span>Azahar Emulator &middot; WebAssembly + WebGL2</span>
      </footer>
    </div>
  );
}

export default App;
