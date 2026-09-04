import {
  Button,
  Callout,
  Dialog,
  DialogBody,
  HTMLTable,
  Icon,
  InputGroup,
  Intent,
  Popover,
  Radio,
  RadioGroup,
  Tag,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useMemo, useState } from "react";
import { useNavigate } from "react-router-dom";
import { ErrorPanel, Loading } from "../../components/Loading";
import { PageHeader } from "../../components/PageHeader";
import type { ScenarioInspectionCatalogEntry } from "../../types/api";
import { ScenarioBuilderDialog } from "./ScenarioBuilderDialog";
import { ScenarioDetailDialog } from "./ScenarioDetailDialog";
import { ScenarioValidationTag } from "./ScenarioViewer";
import { useScenarioLibrary, type ValidationFilter } from "./useScenarioLibrary";

export type ScenarioSortKey = "name" | "type" | "source" | "schema" | "validation" | "updated";
export type SortDirection = "ascending" | "descending";

interface SortState {
  key: ScenarioSortKey;
  direction: SortDirection;
}

const textCollator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });
const validationRank = { invalid: 0, unknown: 1, valid: 2 } as const;

function validationStatus(item: ScenarioInspectionCatalogEntry): keyof typeof validationRank {
  if (item.validation.valid === false) return "invalid";
  if (item.validation.valid === true) return "valid";
  return "unknown";
}

function nullableCompare<T>(left: T | null, right: T | null, compare: (a: T, b: T) => number): number {
  if (left == null && right == null) return 0;
  if (left == null) return 1;
  if (right == null) return -1;
  return compare(left, right);
}

export function sortScenarios(
  items: ScenarioInspectionCatalogEntry[],
  { key, direction }: SortState,
): ScenarioInspectionCatalogEntry[] {
  const factor = direction === "ascending" ? 1 : -1;
  return [...items].sort((left, right) => {
    let compared = 0;
    if (key === "name") compared = textCollator.compare(left.name, right.name);
    if (key === "type") compared = nullableCompare(left.scenario_type, right.scenario_type, textCollator.compare);
    if (key === "source") compared = textCollator.compare(left.source, right.source);
    if (key === "schema") compared = nullableCompare(left.schema_version, right.schema_version, (a, b) => a - b);
    if (key === "validation") compared = validationRank[validationStatus(left)] - validationRank[validationStatus(right)];
    if (key === "updated") {
      const leftTime = left.updated_at == null || !Number.isFinite(Date.parse(left.updated_at)) ? null : Date.parse(left.updated_at);
      const rightTime = right.updated_at == null || !Number.isFinite(Date.parse(right.updated_at)) ? null : Date.parse(right.updated_at);
      compared = nullableCompare(leftTime, rightTime, (a, b) => a - b);
    }
    // Missing values remain at the end in both directions.
    const leftMissing = (key === "type" && left.scenario_type == null)
      || (key === "schema" && left.schema_version == null)
      || (key === "updated" && (left.updated_at == null || !Number.isFinite(Date.parse(left.updated_at))));
    const rightMissing = (key === "type" && right.scenario_type == null)
      || (key === "schema" && right.schema_version == null)
      || (key === "updated" && (right.updated_at == null || !Number.isFinite(Date.parse(right.updated_at))));
    if (leftMissing !== rightMissing) return leftMissing ? 1 : -1;
    return compared * factor;
  });
}

function SortableHeader({
  activeSort,
  label,
  onSort,
  sortKey,
}: {
  activeSort: SortState;
  label: string;
  onSort: (key: ScenarioSortKey) => void;
  sortKey: ScenarioSortKey;
}) {
  const active = activeSort.key === sortKey;
  return <th aria-sort={active ? activeSort.direction : "none"}>
    <button className={`scenario-sort-button${active ? " is-active" : ""}`} onClick={() => onSort(sortKey)} type="button">
      <span>{label}</span>
      {active && <Icon icon={activeSort.direction === "ascending" ? IconNames.SORT_ASC : IconNames.SORT_DESC} size={12} aria-hidden />}
    </button>
  </th>;
}

export function ScenarioLibraryScreen() {
  const navigate = useNavigate();
  const library = useScenarioLibrary();
  const [sort, setSort] = useState<SortState>({ key: "updated", direction: "descending" });
  const {
    catalog, detail, syncStatus, isCatalogLoading, isScenarioDetailLoading,
    syncing, error,
    detailError, search, source, scenarioType, validation, selectedId,
    selected, types, filtered, setSearch, setSource, setScenarioType,
    setValidation, select, closeDetail, syncRemote,
    refreshCatalog,
  } = library;
  const [filtersOpen, setFiltersOpen] = useState(false);
  const [builderOpen, setBuilderOpen] = useState(false);

  const loadingLabel = isCatalogLoading
    ? "Loading scenario catalog..."
    : "Loading scenario...";
  const activeFilterCount = Number(source !== "all")
    + Number(scenarioType !== "all")
    + Number(validation !== "all");
  const sorted = useMemo(() => sortScenarios(filtered, sort), [filtered, sort]);
  const changeSort = (key: ScenarioSortKey) => setSort((current) => ({
    key,
    direction: current.key === key && current.direction === "ascending" ? "descending" : "ascending",
  }));

  if (error && catalog.length === 0) return <main><ErrorPanel message={error} /></main>;
  return <main className="scenario-library-page">
    <Dialog
      canEscapeKeyClose={false}
      canOutsideClickClose={false}
      className="scenario-loading-dialog"
      isCloseButtonShown={false}
      isOpen={isCatalogLoading || isScenarioDetailLoading}
      onClose={() => undefined}
      title="Loading"
    >
      <DialogBody><Loading label={loadingLabel} /></DialogBody>
    </Dialog>
    <PageHeader title="Scenarios" description="Experiment assets validated against the canonical JSB0 contract" actions={<>
      <Button icon={IconNames.ADD} intent={Intent.PRIMARY} onClick={() => setBuilderOpen(true)}>New Scenario</Button>
      {syncStatus?.configured && <Button icon={IconNames.REFRESH} loading={syncing} onClick={syncRemote}>Sync</Button>}
    </>} />
    {error && <Callout compact intent={Intent.WARNING} role="alert">{error}</Callout>}
    {detailError && <Callout compact intent={Intent.DANGER} role="alert">{detailError}</Callout>}
    <div className="scenario-library-content">
      <div className="scenario-library-toolbar" aria-label="Scenario toolbar">
        <InputGroup aria-label="Search scenarios" leftIcon={IconNames.SEARCH} onChange={(event) => setSearch(event.currentTarget.value)} placeholder="Search name, path, type..." value={search} />
        <Popover
          isOpen={filtersOpen}
          onInteraction={setFiltersOpen}
          placement="bottom-end"
          popoverClassName="scenario-filter-popover-shell"
          content={<div className="scenario-filter-popover" aria-label="Scenario filters">
          <section className="scenario-filter-group">
            <h3>Source</h3>
            <RadioGroup aria-label="Source" name="scenario-source-filter" onChange={(event) => setSource(event.currentTarget.value)} selectedValue={source}>
              <Radio label="All" value="all" />
              <Radio label="Bundled" value="bundled" />
              <Radio label="Managed" value="managed" />
              <Radio label="SFTP" value="sftp" />
            </RadioGroup>
          </section>
          <section className="scenario-filter-group">
            <h3>Type</h3>
            <RadioGroup aria-label="Type" name="scenario-type-filter" onChange={(event) => setScenarioType(event.currentTarget.value)} selectedValue={scenarioType}>
              <Radio label="All" value="all" />
              {types.map((item) => <Radio key={item} label={item} value={item} />)}
            </RadioGroup>
          </section>
          <section className="scenario-filter-group">
            <h3>Validation</h3>
            <RadioGroup aria-label="Validation" name="scenario-validation-filter" onChange={(event) => setValidation(event.currentTarget.value as ValidationFilter)} selectedValue={validation}>
              <Radio label="All" value="all" />
              <Radio label="Valid" value="valid" />
              <Radio label="Invalid" value="invalid" />
            </RadioGroup>
          </section>
          <footer className="scenario-filter-footer">
            <Tag minimal>{filtered.length} / {catalog.length}</Tag>
            <div>
              <Button
                disabled={activeFilterCount === 0}
                minimal
                onClick={() => {
                  setSource("all");
                  setScenarioType("all");
                  setValidation("all");
                }}
              >Clear</Button>
              <Button minimal onClick={() => setFiltersOpen(false)}>Done</Button>
            </div>
          </footer>
          </div>}
        >
          <Button
            aria-label={activeFilterCount === 0 ? "Filters" : `Filters, ${activeFilterCount} active`}
            icon={IconNames.FILTER}
            intent={activeFilterCount > 0 ? Intent.PRIMARY : Intent.NONE}
            rightIcon={IconNames.CARET_DOWN}
          >{activeFilterCount === 0 ? "Filters" : `Filters (${activeFilterCount})`}</Button>
        </Popover>
      </div>
      <section className="table-shell scenario-list-pane scenario-list-full-width" aria-label="Scenario list">
        <HTMLTable compact interactive>
          <thead><tr>
            <SortableHeader activeSort={sort} label="Name" onSort={changeSort} sortKey="name" />
            <SortableHeader activeSort={sort} label="Type" onSort={changeSort} sortKey="type" />
            <SortableHeader activeSort={sort} label="Source" onSort={changeSort} sortKey="source" />
            <SortableHeader activeSort={sort} label="Schema" onSort={changeSort} sortKey="schema" />
            <SortableHeader activeSort={sort} label="Validation" onSort={changeSort} sortKey="validation" />
            <SortableHeader activeSort={sort} label="Updated" onSort={changeSort} sortKey="updated" />
          </tr></thead>
          <tbody>{sorted.map((item, index) => <tr
            aria-selected={selectedId === item.id}
            className={`${index % 2 === 0 ? "scenario-row-odd" : "scenario-row-even"}${selectedId === item.id ? " is-selected" : ""}`}
            key={item.id}
            onClick={() => select(item.id)}
            onKeyDown={(event) => {
              if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                select(item.id);
              }
            }}
            tabIndex={0}
          >
            <td><strong>{item.name}</strong><small>{item.path}</small></td>
            <td><code>{item.scenario_type ?? "—"}</code></td>
            <td><Tag minimal>{item.source.toUpperCase()}</Tag></td>
            <td>{item.schema_version == null ? "—" : `v${item.schema_version}`}</td>
            <td><ScenarioValidationTag validation={item.validation} /></td>
            <td className="technical-value">{item.updated_at ? new Date(item.updated_at).toLocaleString() : "—"}</td>
          </tr>)}</tbody>
        </HTMLTable>
        {filtered.length === 0 && <div className="empty-table-state">No scenarios match the current filters.</div>}
      </section>
    </div>
    <ScenarioDetailDialog
      isOpen={selected != null && detail?.id === selected.id && !isScenarioDetailLoading}
      onClose={closeDetail}
      scenario={detail?.id === selected?.id ? detail : null}
    />
    <ScenarioBuilderDialog
      isOpen={builderOpen}
      onClose={() => setBuilderOpen(false)}
      onSaved={async (created) => {
        await refreshCatalog();
        select(`managed:${created.id}`);
      }}
      onRunRequested={async (created) => {
        await refreshCatalog();
        navigate(`/runs?new=1&scenario=${encodeURIComponent(`managed:${created.id}`)}`);
      }}
    />
  </main>;
}
