import { lazy, Suspense } from "react";
import { Navigate, Route, Routes } from "react-router-dom";
import { AppShell } from "./components/AppShell";
import { Loading } from "./components/Loading";
import { RunsPage } from "./pages/RunsPage";

const RunDetailPage = lazy(() =>
  import("./pages/RunDetailPage").then((module) => ({ default: module.RunDetailPage })),
);
const RunComparePage = lazy(() =>
  import("./pages/RunComparePage").then((module) => ({ default: module.RunComparePage })),
);
const SettingsPage = lazy(() =>
  import("./pages/SettingsPage").then((module) => ({ default: module.SettingsPage })),
);
const BuildsPage = lazy(() =>
  import("./pages/BuildsPage").then((module) => ({ default: module.BuildsPage })),
);
const ScenarioLibraryPage = lazy(() =>
  import("./pages/ScenarioLibraryPage").then((module) => ({ default: module.ScenarioLibraryPage })),
);

export function App() {
  return <AppShell>
    <Suspense fallback={<main><Loading label="Loading view" /></main>}>
      <Routes>
        <Route path="/runs" element={<RunsPage />} />
        <Route path="/runs/compare" element={<RunComparePage />} />
        <Route path="/runs/:id" element={<RunDetailPage />} />
        <Route path="/repositories" element={<Navigate to="/settings" replace />} />
        <Route path="/repositories/:id" element={<Navigate to="/settings" replace />} />
        <Route path="/builds" element={<BuildsPage />} />
        <Route path="/scenarios" element={<ScenarioLibraryPage />} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="/compare" element={<Navigate to="/runs" replace />} />
        <Route path="/comparisons/:id" element={<Navigate to="/runs" replace />} />
        <Route path="*" element={<Navigate to="/runs" replace />} />
      </Routes>
    </Suspense>
  </AppShell>;
}
