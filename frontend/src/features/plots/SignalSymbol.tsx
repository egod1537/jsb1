import katex from "katex";
import "katex/dist/katex.min.css";

export function SignalSymbol({ latex, label }: { latex?: string; label?: string }) {
  if (!latex) return <span className="signal-symbol signal-symbol-empty" aria-hidden>—</span>;
  const markup = katex.renderToString(latex, {
    displayMode: false,
    output: "html",
    strict: "ignore",
    throwOnError: false,
    trust: false,
  });
  return <span
    className="signal-symbol"
    aria-label={label ?? latex}
    dangerouslySetInnerHTML={{ __html: markup }}
  />;
}
