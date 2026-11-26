// Shared library navigation and file classification; no device operations here.
export function libraryRoute(route) {
  if (route === 'amps' || route === 'models/amps') return { view: 'models', kind: 'amps' };
  if (route === 'cabs' || route === 'models/cabs') return { view: 'models', kind: 'cabs' };
  return { view: route, kind: null };
}

export function fileKind(name) {
  if (/\.(nam|fcm)$/i.test(name)) return 'amps';
  if (/\.wav$/i.test(name)) return 'cabs';
  throw new Error('Choose a NAM capture (.nam), Frost model (.fcm), or cabinet IR (.wav).');
}

export function matchingSlots(stored, query, offset = 0) {
  const text = query.trim().toLowerCase();
  return Object.keys(stored).map(Number).sort((a, b) => a - b).filter(i =>
    !text || `${String(i + offset).padStart(2, '0')} ${stored[i].label || ''}`.toLowerCase().includes(text));
}

// The explicit post-upload action only changes the originating block's choice.
// Never create/replace an effect, or reuse a target from another connection.
export async function useLibraryFile(view, target, kind, number) {
  if (!target || !view.attached || target.session !== view.live) throw new Error('Reconnect and choose the file in the Editor.');
  const session = target.session, d = await session.slot(target.slot);
  if (session !== view.live) throw new Error('The connection changed. Choose the file in the Editor.');
  if (d?.type !== (kind === 'amps' ? 33 : 3)) throw new Error('That block has changed. Choose a NAM or IR block in the Editor.');
  view.selected = target.slot;
  await view.act(() => kind === 'amps' ? session.setParam(target.slot, 0, number) : session.setCab(target.slot, number), 'value', true);
}
