import { Intent, OverlayToaster, Position, type ToastProps, type Toaster } from "@blueprintjs/core";

let toaster: Promise<Toaster> | undefined;

function getToaster() {
  toaster ??= OverlayToaster.create({ position: Position.TOP_RIGHT });
  return toaster;
}

export async function showOperationToast(props: ToastProps) {
  const instance = await getToaster();
  instance.show({ timeout: 3500, ...props });
}

export function showSuccess(message: string) {
  return showOperationToast({ intent: Intent.SUCCESS, message });
}
