// Deterministic guard for the agent-instruction layer. Mirrors clwill-os.
// - every AGENTS.md has an exact `@AGENTS.md` CLAUDE.md shim and vice versa
// - every .agents/skills/<name>/SKILL.md has frontmatter with name == <name> and a description
// - every skill has a .claude/skills/<name> symlink to ../../.agents/skills/<name>
import { lstat, readdir, readFile, readlink } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const root = process.cwd();
const errors = [];
const instructionFiles = [];
const skipped = new Set(['.git', '.agents', '.claude', 'node_modules', '.esphome', '.pio', 'build', 'dist']);

async function exists(p) { try { await lstat(p); return true; } catch (e) { if (e?.code === 'ENOENT') return false; throw e; } }

async function walk(dir) {
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    if (entry.isDirectory() && skipped.has(entry.name)) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) await walk(full);
    else if (entry.name === 'AGENTS.md' || entry.name === 'CLAUDE.md') instructionFiles.push(full);
  }
}
await walk(root);

for (const dir of new Set(instructionFiles.map((f) => path.dirname(f)))) {
  const agents = path.join(dir, 'AGENTS.md');
  const claude = path.join(dir, 'CLAUDE.md');
  const rel = path.relative(root, dir) || '.';
  if (!(await exists(agents))) errors.push(`${rel}: CLAUDE.md has no canonical AGENTS.md`);
  if (!(await exists(claude))) errors.push(`${rel}: AGENTS.md has no Claude compatibility shim`);
  else if ((await readFile(claude, 'utf8')) !== '@AGENTS.md\n') errors.push(`${rel}/CLAUDE.md: expected exact @AGENTS.md shim`);
}

const skillsRoot = path.join(root, '.agents', 'skills');
const skills = (await readdir(skillsRoot, { withFileTypes: true })).filter((e) => e.isDirectory()).map((e) => e.name).sort();
for (const skill of skills) {
  const skillPath = path.join(skillsRoot, skill, 'SKILL.md');
  if (!(await exists(skillPath))) { errors.push(`.agents/skills/${skill}/SKILL.md: missing`); continue; }
  const fm = (await readFile(skillPath, 'utf8')).match(/^---\n([\s\S]*?)\n---\n/);
  if (!fm) errors.push(`.agents/skills/${skill}/SKILL.md: missing YAML frontmatter`);
  else {
    if (fm[1].match(/^name:\s*(.+)$/m)?.[1]?.trim() !== skill) errors.push(`.agents/skills/${skill}/SKILL.md: name must be ${skill}`);
    if (!fm[1].match(/^description:\s*(.+)$/m)?.[1]?.trim()) errors.push(`.agents/skills/${skill}/SKILL.md: description is required`);
  }
  const link = path.join(root, '.claude', 'skills', skill);
  const expected = `../../.agents/skills/${skill}`;
  if (!(await exists(link))) errors.push(`.claude/skills/${skill}: missing compatibility symlink`);
  else {
    const st = await lstat(link);
    if (!st.isSymbolicLink()) errors.push(`.claude/skills/${skill}: must be a symlink`);
    else {
      const target = await readlink(link);
      if (target !== expected) errors.push(`.claude/skills/${skill}: expected ${expected}, got ${target}`);
      if (!(await exists(path.resolve(path.dirname(link), target)))) errors.push(`.claude/skills/${skill}: symlink target does not resolve`);
    }
  }
}

if (errors.length) { for (const e of errors) console.error(`ERROR ${e}`); process.exit(1); }
console.log(`ai-tooling OK: ${instructionFiles.length} instruction files, ${skills.length} skills`);
