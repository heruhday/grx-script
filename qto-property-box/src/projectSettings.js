import { invoke } from '@tauri-apps/api/core';

const pendingWrites = new Map();

export function saveProjectSetting(projectPath, key, value) {
  const identity = JSON.stringify([projectPath, key]);
  const pending = (pendingWrites.get(identity) || Promise.resolve()).catch(() => {})
    .then(() => invoke('save_project_setting', { projectPath, key, value }));
  pendingWrites.set(identity, pending);
  const cleanup = () => { if (pendingWrites.get(identity) === pending) pendingWrites.delete(identity); };
  pending.then(cleanup, cleanup);
  return pending;
}
