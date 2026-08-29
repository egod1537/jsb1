import { Alert, Intent } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";

interface ConfirmActionProps {
  isOpen: boolean;
  title: string;
  message: string;
  confirmLabel: string;
  loading?: boolean;
  onCancel: () => void;
  onConfirm: () => void;
}

export function ConfirmAction({
  isOpen,
  title,
  message,
  confirmLabel,
  loading = false,
  onCancel,
  onConfirm,
}: ConfirmActionProps) {
  return (
    <Alert
      cancelButtonText="Cancel"
      canEscapeKeyCancel
      canOutsideClickCancel
      confirmButtonText={confirmLabel}
      icon={IconNames.WARNING_SIGN}
      intent={Intent.DANGER}
      isOpen={isOpen}
      loading={loading}
      onCancel={onCancel}
      onConfirm={onConfirm}
    >
      <h3>{title}</h3>
      <p>{message}</p>
    </Alert>
  );
}
