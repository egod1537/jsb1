import { Callout, Intent, Spinner } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";

export function Loading({ label = "Loading" }: { label?: string }) {
  return (
    <div className="loading" role="status">
      <Spinner size={16} />
      {label}
    </div>
  );
}

export function ErrorPanel({ message }: { message: string }) {
  return (
    <Callout className="error-panel" icon={IconNames.ERROR} intent={Intent.DANGER} title="Could not load data" role="alert">
      {message}
    </Callout>
  );
}
