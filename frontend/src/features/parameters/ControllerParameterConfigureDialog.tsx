import { Button, Callout, Dialog, DialogBody, DialogFooter, Intent } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useEffect, useMemo, useState } from "react";
import type { ControllerParameterDefinition } from "../../types/api";
import {
  ControllerParameterEditor,
  controllerParameterCategories,
  controllerParameterCategory,
  defaultControllerParameterDraftValues,
  parseControllerParameterDrafts,
  type ControllerParameterDraftValues,
} from "./ControllerParameterEditor";
import "./parameters.css";

interface Props {
  definitions: ControllerParameterDefinition[];
  isOpen: boolean;
  onApply: (values: Record<string, number>) => void;
  onCancel: () => void;
  values: Record<string, number>;
}

function draftsFromValues(
  definitions: ControllerParameterDefinition[],
  values: Record<string, number>,
): ControllerParameterDraftValues {
  const defaults = defaultControllerParameterDraftValues(definitions);
  return Object.fromEntries(Object.entries(defaults).map(([id, defaultValue]) => [
    id,
    Number.isFinite(values[id]) ? String(values[id]) : defaultValue,
  ]));
}

export function ControllerParameterConfigureDialog({
  definitions,
  isOpen,
  onApply,
  onCancel,
  values,
}: Props) {
  const categories = useMemo(
    () => controllerParameterCategories(definitions),
    [definitions],
  );
  const [selectedCategoryId, setSelectedCategoryId] = useState<string>();
  const [draftValues, setDraftValues] = useState<ControllerParameterDraftValues>({});
  const [touched, setTouched] = useState<Record<string, boolean>>({});
  const [applyAttempted, setApplyAttempted] = useState(false);
  const parsed = useMemo(
    () => parseControllerParameterDrafts(definitions, draftValues),
    [definitions, draftValues],
  );
  const visibleErrors = Object.fromEntries(Object.entries(parsed.errors)
    .filter(([id]) => applyAttempted || touched[id]));

  useEffect(() => {
    if (!isOpen) return;
    setDraftValues(draftsFromValues(definitions, values));
    setTouched({});
    setApplyAttempted(false);
    setSelectedCategoryId((current) => categories.some((category) => category.id === current)
      ? current
      : categories[0]?.id);
  }, [categories, definitions, isOpen, values]);

  function applyDrafts() {
    if (Object.keys(parsed.errors).length > 0) {
      setApplyAttempted(true);
      return;
    }
    onApply(parsed.values);
  }

  return <Dialog
    className="controller-parameters-dialog controller-parameters-configure-dialog"
    icon={IconNames.PROPERTIES}
    isOpen={isOpen}
    onClose={onCancel}
    title="Configure Controller Parameters"
  >
    <DialogBody className="controller-parameters-configure-body">
      <div className="controller-parameters-configure-layout">
        <aside className="controller-parameter-categories" aria-label="Parameter categories">
          <header>Categories</header>
          <nav>
            {categories.map((category) => {
              const count = definitions.filter((definition) =>
                controllerParameterCategory(definition).id === category.id).length;
              return <button
                aria-pressed={selectedCategoryId === category.id}
                className={selectedCategoryId === category.id ? "is-active" : undefined}
                key={category.id}
                onClick={() => setSelectedCategoryId(category.id)}
                type="button"
              >
                <span>{category.label}</span>
                <small>{count}</small>
              </button>;
            })}
          </nav>
        </aside>
        <main className="controller-parameter-edit-pane">
          <ControllerParameterEditor
            definitions={definitions}
            draftValues={draftValues}
            fieldErrors={visibleErrors}
            onBlur={(id) => setTouched((current) => ({ ...current, [id]: true }))}
            onChange={setDraftValues}
            selectedCategoryId={selectedCategoryId}
          />
          {applyAttempted && Object.keys(parsed.errors).length > 0 && <Callout compact intent={Intent.DANGER}>
            Correct the highlighted parameter values before applying.
          </Callout>}
        </main>
      </div>
    </DialogBody>
    <DialogFooter actions={<>
      <Button onClick={onCancel}>Cancel</Button>
      <Button intent={Intent.PRIMARY} onClick={applyDrafts}>Apply</Button>
    </>} />
  </Dialog>;
}
