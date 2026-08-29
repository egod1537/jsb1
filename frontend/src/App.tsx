import { lazy, Suspense } from "react";
import { NavLink, Navigate, Route, Routes } from "react-router-dom";
import { Loading } from "./components/Loading";
import { RunsPage } from "./pages/RunsPage";

const RunDetailPage = lazy(() =>
  import("./pages/RunDetailPage").then((module) => ({ default: module.RunDetailPage })),
);
const ComparePage = lazy(() =>
  import("./pages/ComparePage").then((module) => ({ default: module.ComparePage })),
);
const RepositoriesPage = lazy(() =>
  import("./pages/RepositoriesPage").then((module) => ({ default: module.RepositoriesPage })),
);
const RepositoryDetailPage = lazy(() =>
  import("./pages/RepositoryDetailPage").then((module) => ({ default: module.RepositoryDetailPage })),
);
const BuildsPage = lazy(() =>
  import("./pages/BuildsPage").then((module) => ({ default: module.BuildsPage })),
);
const DeploymentsPage = lazy(() =>
  import("./pages/DeploymentsPage").then((module) => ({ default: module.DeploymentsPage })),
);

export function App() {
  return <div className="app-shell">
    <header className="topbar">
      <NavLink to="/runs" className="brand"><span>J1</span><div><strong>JSB1</strong><small>Regression server</small></div></NavLink>
      <nav><NavLink to="/runs">Runs</NavLink><NavLink to="/repositories">Repositories</NavLink><NavLink to="/builds">Builds</NavLink><NavLink to="/deployments">Deployments</NavLink><NavLink to="/compare">Compare</NavLink></nav>
      <div className="host"><i /> Mac mini</div>
    </header>
    <Suspense fallback={<main><Loading label="Loading view" /></main>}>
      <Routes>
        <Route path="/runs" element={<RunsPage />} />
        <Route path="/runs/:id" element={<RunDetailPage />} />
        <Route path="/repositories" element={<RepositoriesPage />} />
        <Route path="/repositories/:id" element={<RepositoryDetailPage />} />
        <Route path="/builds" element={<BuildsPage />} />
        <Route path="/deployments" element={<DeploymentsPage />} />
        <Route path="/compare" element={<ComparePage />} />
        <Route path="*" element={<Navigate to="/runs" replace />} />
      </Routes>
    </Suspense>
  </div>;
}
