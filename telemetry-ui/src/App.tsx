import { Cable } from "lucide-react";

export function App() {
  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand">
          <Cable size={18} aria-hidden="true" />
          <h1>nav-mcu telemetry</h1>
        </div>
        <span className="status-pill status-off">disconnected</span>
      </header>
      <section className="empty-state">
        <h2>Telemetry UI</h2>
        <p>React control app scaffold.</p>
      </section>
    </main>
  );
}
