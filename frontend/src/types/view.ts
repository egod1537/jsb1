/** Shared presentation state used by charts and the plots feature. */
export interface TimelineState {
  visibleStart: number;
  visibleEnd: number;
  cursorTime: number | null;
  selectedRange: [number, number] | null;
}
