import { Button, Callout, Dialog, DialogBody, DialogFooter, Intent } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { ControllerParameterDefinition } from "../../types/api";
import { ControllerParameterEditor, controllerParameterErrors } from "./ControllerParameterEditor";
import "./parameters.css";

interface Props {
  definitions: ControllerParameterDefinition[];
  isOpen: boolean;
  onApply: () => void;
  onCancel: () => void;
  onChange: (values: Record<string, number>) => void;
  values: Record<string, number>;
  variants: string[];
}

export function ControllerParameterConfigureDialog({
  definitions,
  isOpen,
  onApply,
  onCancel,
  onChange,
  values,
  variants,
}: Props) {
  const errors = controllerParameterErrors(definitions, values, variants);
  return <Dialog
    className="controller-parameters-dialog"
    icon={IconNames.PROPERTIES}
    isOpen={isOpen}
    onClose={onCancel}
    title="Configure Controller Parameters"
  >
    <DialogBody>
      <ControllerParameterEditor
        definitions={definitions}
        onChange={onChange}
        values={values}
        variants={variants}
      />
      {errors.length > 0 && <Callout compact intent={Intent.DANGER}>{errors.join("; ")}</Callout>}
    </DialogBody>
    <DialogFooter actions={<>
      <Button onClick={onCancel}>Cancel</Button>
      <Button disabled={errors.length > 0} intent={Intent.PRIMARY} onClick={onApply}>Apply</Button>
    </>} />
  </Dialog>;
}
