import { Intent, Tag, type Intent as IntentType } from "@blueprintjs/core";
import { IconNames, type IconName } from "@blueprintjs/icons";

const statusPresentation: Record<string, { intent: IntentType; icon: IconName }> = {
  completed: { intent: Intent.SUCCESS, icon: IconNames.TICK_CIRCLE },
  healthy: { intent: Intent.SUCCESS, icon: IconNames.TICK_CIRCLE },
  connected: { intent: Intent.SUCCESS, icon: IconNames.LINK },
  running: { intent: Intent.SUCCESS, icon: IconNames.PLAY },
  queued: { intent: Intent.WARNING, icon: IconNames.TIME },
  starting: { intent: Intent.WARNING, icon: IconNames.REFRESH },
  warning: { intent: Intent.WARNING, icon: IconNames.WARNING_SIGN },
  dirty: { intent: Intent.WARNING, icon: IconNames.WARNING_SIGN },
  failed: { intent: Intent.DANGER, icon: IconNames.ERROR },
  partial_failed: { intent: Intent.WARNING, icon: IconNames.WARNING_SIGN },
  error: { intent: Intent.DANGER, icon: IconNames.ERROR },
  disconnected: { intent: Intent.DANGER, icon: IconNames.OFFLINE },
  stopped: { intent: Intent.NONE, icon: IconNames.STOP },
  checking: { intent: Intent.NONE, icon: IconNames.MORE },
  clean: { intent: Intent.NONE, icon: IconNames.TICK },
};

export function StatusTag({ status }: { status: string }) {
  const normalized = status.toLowerCase();
  const presentation = statusPresentation[normalized] ?? { intent: Intent.NONE, icon: IconNames.DOT };
  return (
    <Tag className="status-tag" icon={presentation.icon} intent={presentation.intent} minimal>
      {status}
    </Tag>
  );
}
