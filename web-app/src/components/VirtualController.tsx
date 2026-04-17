import { useCallback, useEffect, useRef } from 'react';

// 3DS button → SDL scancode mapping (matches the defaults in src/citra_sdl/config.cpp).
// We send the event's `code` (for SDL's DOM event handler) and a matching `keyCode`
// for older paths. SDL2's Emscripten port listens to key events on `document`.
type ButtonSpec = {
  label: string;
  code: string;
  keyCode: number;
  key: string;
};

const B = (label: string, code: string, keyCode: number, key: string): ButtonSpec => ({
  label, code, keyCode, key,
});

const FACE = {
  A: B('A', 'KeyA', 65, 'a'),
  B: B('B', 'KeyS', 83, 's'),
  X: B('X', 'KeyZ', 90, 'z'),
  Y: B('Y', 'KeyX', 88, 'x'),
};

const DPAD = {
  UP: B('▲', 'KeyT', 84, 't'),
  DOWN: B('▼', 'KeyG', 71, 'g'),
  LEFT: B('◀', 'KeyF', 70, 'f'),
  RIGHT: B('▶', 'KeyH', 72, 'h'),
};

// Circle Pad (analog) — defaults map to arrow keys in citra_sdl/config.cpp
const CPAD = {
  UP: B('↑', 'ArrowUp', 38, 'ArrowUp'),
  DOWN: B('↓', 'ArrowDown', 40, 'ArrowDown'),
  LEFT: B('←', 'ArrowLeft', 37, 'ArrowLeft'),
  RIGHT: B('→', 'ArrowRight', 39, 'ArrowRight'),
};

const SHOULDER = {
  L: B('L', 'KeyQ', 81, 'q'),
  R: B('R', 'KeyW', 87, 'w'),
};

const SYSTEM = {
  SELECT: B('SELECT', 'KeyN', 78, 'n'),
  START: B('START', 'KeyM', 77, 'm'),
};

function dispatchKey(type: 'keydown' | 'keyup', btn: ButtonSpec) {
  const ev = new KeyboardEvent(type, {
    key: btn.key,
    code: btn.code,
    keyCode: btn.keyCode,
    which: btn.keyCode,
    bubbles: true,
    cancelable: true,
    repeat: false,
  });
  // SDL2's Emscripten port registers its handler on `document`.
  document.dispatchEvent(ev);
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
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
    if (!pressedRef.current) {
      pressedRef.current = true;
      dispatchKey('keydown', btn);
    }
  }, [btn]);

  const release = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    e.preventDefault();
    try { (e.currentTarget as Element).releasePointerCapture?.(e.pointerId); } catch { /* empty */ }
    if (pressedRef.current) {
      pressedRef.current = false;
      dispatchKey('keyup', btn);
    }
  }, [btn]);

  // Safety: release on unmount if still pressed
  useEffect(() => () => {
    if (pressedRef.current) {
      pressedRef.current = false;
      dispatchKey('keyup', btn);
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
