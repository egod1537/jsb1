import type { ReactNode } from "react";

interface PageHeaderProps {
  eyebrow?: string;
  title: string;
  description?: string;
  actions?: ReactNode;
}

export function PageHeader({ eyebrow, title, description, actions }: PageHeaderProps) {
  return (
    <header className="page-heading">
      <div className="page-heading-copy">
        <div className="page-title-line">
          <h1>{title}</h1>
          {eyebrow && <span className="eyebrow">{eyebrow}</span>}
        </div>
        {description && <p>{description}</p>}
      </div>
      {actions && <div className="page-toolbar" aria-label={`${title} actions`}>{actions}</div>}
    </header>
  );
}
