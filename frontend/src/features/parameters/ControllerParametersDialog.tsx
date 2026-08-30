import { Dialog, DialogBody, HTMLTable, Tag } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import type { ControllerParameterDefinition } from "../../types/api";
import "./parameters.css";

interface Props {
  definitions?: ControllerParameterDefinition[];
  isOpen: boolean;
  onClose: () => void;
  parameters: Record<string, number>;
  title?: string;
}

export function ControllerParametersDialog({ definitions = [], isOpen, onClose, parameters, title = "Controller Parameters" }: Props) {
  const metadata = new Map(definitions.map((item) => [item.id, item]));
  return <Dialog className="controller-parameters-dialog" icon={IconNames.PROPERTIES} isOpen={isOpen} onClose={onClose} title={title}>
    <DialogBody>
      {Object.keys(parameters).length === 0
        ? <div className="empty-table-state">This Run did not use an external controller parameter set.</div>
        : <HTMLTable compact striped>
          <thead><tr><th>Parameter</th><th>Canonical ID</th><th>Value</th><th>Unit</th></tr></thead>
          <tbody>{Object.entries(parameters).map(([id, value]) => {
            const item = metadata.get(id);
            return <tr key={id}>
              <td>{item?.display_name ?? id}</td>
              <td><code>{id}</code></td>
              <td className="technical-value">{value}</td>
              <td>{item?.unit ?? <Tag minimal>N/A</Tag>}</td>
            </tr>;
          })}</tbody>
        </HTMLTable>}
    </DialogBody>
  </Dialog>;
}
