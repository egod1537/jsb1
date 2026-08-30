import type { TimelineState } from "./plotTypes";

// Timeline state remains independent of any Run or Comparison transport.

export type TimelineAction =
  | { type: "set-range"; start: number; end: number }
  | { type: "set-cursor"; time: number | null }
  | { type: "set-selection"; range: [number, number] | null }
  | { type: "reset"; start: number; end: number };

export function createTimeline(start = 0, end = 0): TimelineState {
  return {
    visibleStart: start,
    visibleEnd: end,
    cursorTime: null,
    selectedRange: null,
  };
}

export function timelineReducer(state: TimelineState, action: TimelineAction): TimelineState {
  switch (action.type) {
    case "set-range": {
      if (!Number.isFinite(action.start) || !Number.isFinite(action.end) || action.start >= action.end) return state;
      if (state.visibleStart === action.start && state.visibleEnd === action.end) return state;
      return { ...state, visibleStart: action.start, visibleEnd: action.end };
    }
    case "set-cursor":
      return state.cursorTime === action.time ? state : { ...state, cursorTime: action.time };
    case "set-selection":
      return { ...state, selectedRange: action.range };
    case "reset":
      return createTimeline(action.start, action.end);
  }
}
