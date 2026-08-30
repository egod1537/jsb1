import {
  Alignment,
  Classes,
  Icon,
  Menu,
  MenuItem,
  Navbar,
  NavbarGroup,
  NavbarHeading,
  NavbarDivider,
  Tag,
  Tooltip,
} from "@blueprintjs/core";
import { IconNames, type IconName } from "@blueprintjs/icons";
import { useEffect, useState, type ReactNode } from "react";
import { NavLink, useLocation, useNavigate } from "react-router-dom";
import { api } from "../api/client";
import { buildInfo } from "../buildInfo";
import type { BuildVersion } from "../types/api";
import { DeploymentRevision } from "./DeploymentRevision";
import { StatusTag } from "./StatusTag";

interface NavigationItem {
  label: string;
  path: string;
  icon: IconName;
}

interface NavigationSection {
  label: string;
  icon: IconName;
  items: NavigationItem[];
}

const navigationSections: NavigationSection[] = [
  {
    label: "Operations",
    icon: IconNames.CONTROL,
    items: [
      { label: "Runs", path: "/runs", icon: IconNames.AIRPLANE },
    ],
  },
  {
    label: "Assets",
    icon: IconNames.DOCUMENT,
    items: [
      { label: "Scenarios", path: "/scenarios", icon: IconNames.DOCUMENT },
    ],
  },
  {
    label: "System",
    icon: IconNames.COG,
    items: [
      { label: "Settings", path: "/settings", icon: IconNames.COG },
    ],
  },
];
const navigation = navigationSections.flatMap((section) => section.items);

function isActive(pathname: string, path: string) {
  return pathname === path || pathname.startsWith(`${path}/`);
}

export function AppShell({ children }: { children: ReactNode }) {
  const location = useLocation();
  const navigate = useNavigate();
  const current = navigation.find((item) => isActive(location.pathname, item.path));
  const [backendVersion, setBackendVersion] = useState<BuildVersion | null>(null);
  const [backendConnected, setBackendConnected] = useState<boolean | null>(null);

  useEffect(() => {
    let active = true;
    void api.version()
      .then((version) => {
        if (active) {
          setBackendVersion(version);
          setBackendConnected(true);
        }
      })
      .catch(() => { if (active) setBackendConnected(false); });
    return () => { active = false; };
  }, []);

  return (
    <div className={`${Classes.DARK} app-shell`}>
      <Navbar className="app-navbar">
        <NavbarGroup align={Alignment.LEFT}>
          <NavLink to="/runs" className="brand" aria-label="JSB1 runs">
            <span aria-hidden="true">J1</span>
            <NavbarHeading className="brand-title">JSB1</NavbarHeading>
          </NavLink>
          <NavbarDivider />
          <span className="current-section">{current?.label ?? "Console"}</span>
        </NavbarGroup>
        <NavbarGroup align={Alignment.RIGHT}>
          <Tooltip content="Backend API connection">
            <span><StatusTag status={backendConnected === null ? "checking" : backendConnected ? "connected" : "disconnected"} /></span>
          </Tooltip>
          <span className="navbar-host technical-value" title={backendVersion?.hostname ?? buildInfo.hostname ?? "Local development"}>
            {backendVersion?.hostname ?? buildInfo.hostname ?? "localhost"}
          </span>
          <NavbarDivider />
          <DeploymentRevision backendVersion={backendVersion} />
          <Tag className="runtime-target" icon={IconNames.DESKTOP} minimal>
            Mac mini
          </Tag>
        </NavbarGroup>
      </Navbar>
      <div className="app-workspace">
        <aside className="sidebar">
          <nav aria-label="Primary navigation">
            {navigationSections.map((section) => <section className="sidebar-section" key={section.label} aria-labelledby={`sidebar-${section.label.toLowerCase()}`}>
              <div className="sidebar-title" id={`sidebar-${section.label.toLowerCase()}`}>
                <Icon icon={section.icon} size={14} />
                {section.label}
              </div>
              <Menu className="sidebar-menu">
                {section.items.map((item) => (
                  <MenuItem
                    active={isActive(location.pathname, item.path)}
                    aria-current={isActive(location.pathname, item.path) ? "page" : undefined}
                    href={item.path}
                    icon={item.icon}
                    key={item.path}
                    onClick={(event) => {
                      if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
                      event.preventDefault();
                      navigate(item.path);
                    }}
                    text={item.label}
                  />
                ))}
              </Menu>
            </section>)}
          </nav>
        </aside>
        <div className="app-content">{children}</div>
      </div>
    </div>
  );
}
