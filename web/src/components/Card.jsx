// The white rounded box every panel sits in. `children` is whatever JSX is nested inside
// <Card>...</Card>; `aside` is optional content for the right side of the header.
export default function Card({ title, icon, aside, children, className = '' }) {
  return (
    <section className={`card ${className}`}>
      <header className="card-header">
        <h2>
          {icon && <span className="card-icon" aria-hidden="true">{icon}</span>}
          {title}
        </h2>
        {aside}
      </header>
      {children}
    </section>
  );
}
