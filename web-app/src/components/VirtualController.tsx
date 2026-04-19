import { useCallback, useEffect, useRef } from 'react';

// 3DS button → SDL scancode mapping. Must match the defaults seeded in
// src/citra_emc/citra_emc.cpp (InitDefaultInputProfile) and in the sibling
// src/citra_sdl/config.cpp. Values come from SDL_scancode.h.
const SDL = {
  A: 4,  B: 5,  D: 7,  F: 9,  G: 10, H: 11, I: 12, J: 13, K: 14, L: 15,
  M: 16, N: 17, O: 18, P: 19, Q: 20, S: 22, T: 23, W: 26, X: 27, Y: 28,
  Z: 29,
  ONE: 30, TWO: 31,
  RIGHT: 79, LEFT: 80, DOWN: 81, UP: 82,
} as const;

type ButtonSpec = {
  label: string;
  scancode: number;
};

const B = (label: string, scancode: number): ButtonSpec => ({ label, scancode });

const FACE = {
  A: B('A', SDL.A),
  B: B('B', SDL.S),
  X: B('X', SDL.Z),
  Y: B('Y', SDL.X),
};

const DPAD = {
  UP: B('▲', SDL.T),
  DOWN: B('▼', SDL.G),
  LEFT: B('◀', SDL.F),
  RIGHT: B('▶', SDL.H),
};

// Circle Pad (analog). Defaults map to arrow keys, matching
// InitDefaultInputProfile's default_analogs[0].
const CPAD = {
  UP: B('↑', SDL.UP),
  DOWN: B('↓', SDL.DOWN),
  LEFT: B('←', SDL.LEFT),
  RIGHT: B('→', SDL.RIGHT),
};

const SHOULDER = {
  L: B('L', SDL.Q),
  R: B('R', SDL.W),
};

const SYSTEM = {
  SELECT: B('SELECT', SDL.N),
  START: B('START', SDL.M),
};

type AzaharModule = {
  ccall: (name: string, returnType: string | null, argTypes: string[], args: unknown[]) => unknown;
};

function getModule(): AzaharModule | null {
  return (window as unknown as { __azahar?: AzaharModule }).__azahar ?? null;
}

function sendKey(type: 'down' | 'up', scancode: number) {
  const mod = getModule();
  if (!mod) return;
  try {
    mod.ccall(type === 'down' ? 'EmcKeyDown' : 'EmcKeyUp', null, ['number'], [scancode]);
  } catch {
    // ignore — likely called before ccall is ready
  }
}

interface PadButtonProps {
  btn: ButtonSpec;
  className?: string;
  children?: React.ReactNode;
}

function PadButton({ btn, className, children }: PadButtonProps) {
  const pressedRef = useRef(false);

  const press = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    e.preventDefault();
    e.stopPropagation();
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
    if (!pressedRef.current) {
      pressedRef.current = true;
      sendKey('down', btn.scancode);
    }
  }, [btn]);

  const release = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    e.preventDefault();
    e.stopPropagation();
    try { (e.currentTarget as Element).releasePointerCapture?.(e.pointerId); } catch { /* empty */ }
    if (pressedRef.current) {
      pressedRef.current = false;
      sendKey('up', btn.scancode);
    }
  }, [btn]);

  // Safety: release on unmount if still pressed
  useEffect(() => () => {
    if (pressedRef.current) {
      pressedRef.current = false;
      sendKey('up', btn.scancode);
    }
  }, [btn]);

  return (
    <button
      type="button"
      className={`pad-btn ${className ?? ''}`}
      onPointerDown={press}
      onPointerUp={release}
      onPointerCancel={release}
      onLostPointerCapture={release}
      onContextMenu={(e) => e.preventDefault()}
    >
      {children ?? btn.label}
    </button>
  );
}

export function VirtualController() {
  return (
    <div className="virtual-controller" role="group" aria-label="On-screen controller">
      <div className="pad-shoulders">
        <PadButton btn={SHOULDER.L} className="pad-shoulder" />
        <PadButton btn={SHOULDER.R} className="pad-shoulder" />
      </div>

      <div className="pad-row">
        <div className="pad-dpad">
          <PadButton btn={DPAD.UP}    className="pad-dpad-up" />
          <PadButton btn={DPAD.LEFT}  className="pad-dpad-left" />
          <PadButton btn={DPAD.RIGHT} className="pad-dpad-right" />
          <PadButton btn={DPAD.DOWN}  className="pad-dpad-down" />
        </div>

        <div className="pad-system">
          <PadButton btn={SYSTEM.SELECT} className="pad-system-btn" />
          <PadButton btn={SYSTEM.START}  className="pad-system-btn" />
        </div>

        <div className="pad-face">
          <PadButton btn={FACE.X} className="pad-face-btn pad-face-x" />
          <PadButton btn={FACE.Y} className="pad-face-btn pad-face-y" />
          <PadButton btn={FACE.A} className="pad-face-btn pad-face-a" />
          <PadButton btn={FACE.B} className="pad-face-btn pad-face-b" />
        </div>
      </div>

      <div className="pad-cpad-row">
        <span className="pad-cpad-label">Circle Pad</span>
        <div className="pad-cpad">
          <PadButton btn={CPAD.UP}    className="pad-cpad-up" />
          <PadButton btn={CPAD.LEFT}  className="pad-cpad-left" />
          <PadButton btn={CPAD.RIGHT} className="pad-cpad-right" />
          <PadButton btn={CPAD.DOWN}  className="pad-cpad-down" />
        </div>
      </div>
    </div>
  );
}
