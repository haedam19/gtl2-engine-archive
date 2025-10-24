// scripts/create_issues.js
const fs = require('fs');
// const core = require('@actions/core'); // <- 사용 안하므로 제거 권장
const { Octokit } = require("@octokit/rest");
const { graphql } = require("@octokit/graphql");

(async () => {
  try {
    const token = process.env.GITHUB_TOKEN;
    if (!token) throw new Error("GITHUB_TOKEN is not set.");
    const octokit = new Octokit({ auth: token });
    const ghql = graphql.defaults({
      headers: { authorization: `token ${token}` }
    });

    const manifestPath = process.env.MANIFEST_PATH || 'issue_manifest.json';
    const dryRun = String(process.env.DRY_RUN || 'false').toLowerCase() === 'true';

    const raw = fs.readFileSync(manifestPath, 'utf-8');
    const manifest = JSON.parse(raw);

    // repo 우선순위: manifest.repo > env:GITHUB_REPOSITORY
    const envRepo = process.env.GITHUB_REPOSITORY || '';
    const repoFull = manifest.repo && manifest.repo.includes('/')
      ? manifest.repo
      : (envRepo.includes('/') ? envRepo : null);

    if (!repoFull) throw new Error("repo is not set. Set manifest.repo or GITHUB_REPOSITORY.");
    const [owner, repo] = repoFull.split('/');

    const projectV2Id = (manifest.projectV2Id || '').trim();
    const defaultLabels = manifest.defaultLabels || [];
    const items = manifest.issues || [];

    const byKey = new Map();
    items.forEach(it => {
      if (!it.key) throw new Error("Every issue needs a unique 'key'.");
      if (byKey.has(it.key)) throw new Error(`Duplicate key: ${it.key}`);
      byKey.set(it.key, it);
    });

    const results = {}; // key -> {number, url}

    // Pass 1: create issues
    for (const it of items) {
      const title = it.title;
      const bodyIntro = it.body || '';
      const labels = Array.from(new Set([...(it.labels || []), ...defaultLabels]));
      const assignees = it.assignees || [];

      if (dryRun) {
        console.log(`[DRY] Would create issue: ${it.key} :: ${title}`);
        continue;
      }

      const created = await octokit.rest.issues.create({
        owner, repo, title,
        body: bodyIntro,
        labels, assignees
      });

      results[it.key] = { number: created.data.number, url: created.data.html_url };
      console.log(`Created #${created.data.number} for key=${it.key}`);
    }

    if (dryRun) {
      console.log("[DRY] Skipping Pass 2 (update bodies / project linking)");
      return;
    }

    // Build dependency maps
    const blocksMap = new Map();
    const blockedByMap = new Map();
    for (const it of items) {
      blocksMap.set(it.key, it.blocks || []);
      for (const b of (it.blocks || [])) {
        const arr = blockedByMap.get(b) || [];
        arr.push(it.key);
        blockedByMap.set(b, arr);
      }
    }

    const ref = (k) => results[k] ? `#${results[k].number}` : `(pending:${k})`;

    // Pass 2: update issue bodies + comment checklists
    for (const it of items) {
      if (!results[it.key]) continue; // should not happen unless dry-run/partial

      const number = results[it.key].number;
      const orig = await octokit.rest.issues.get({ owner, repo, issue_number: number });
      const prevBody = orig.data.body || "";

      const blockedByKeys = blockedByMap.get(it.key) || [];
      const blockingKeys  = blocksMap.get(it.key) || [];

      const depSection = [
        `---`,
        `**Blocked by**: ${blockedByKeys.length ? blockedByKeys.map(ref).join(", ") : "None"}`,
        `**Blocking**: ${blockingKeys.length ? blockingKeys.map(ref).join(", ") : "None"}`
      ].join("\n");

      const newBody = `${prevBody}\n\n${depSection}\n`;

      await octokit.rest.issues.update({
        owner, repo, issue_number: number, body: newBody
      });

      const lines = [];
      if (blockedByKeys.length) {
        lines.push(`**Blocked by**`);
        blockedByKeys.forEach(k => lines.push(`- [ ] ${ref(k)} must be completed`));
      }
      if (blockingKeys.length) {
        lines.push(`\n**Blocks**`);
        blockingKeys.forEach(k => lines.push(`- ${ref(k)}`));
      }
      if (lines.length) {
        await octokit.rest.issues.createComment({
          owner, repo, issue_number: number,
          body: lines.join("\n")
        });
      }
    }

    // Optional: add to Project V2
    if (projectV2Id) {
      for (const it of items) {
        if (!results[it.key]) continue;
        const number = results[it.key].number;
        const { data } = await octokit.rest.issues.get({ owner, repo, issue_number: number });
        const nodeId = data.node_id;

        await ghql(`
          mutation($projectId: ID!, $contentId: ID!) {
            addProjectV2ItemById(input: {projectId: $projectId, contentId: $contentId}) {
              item { id }
            }
          }
        `, { projectId: projectV2Id, contentId: nodeId });

        console.log(`Added #${number} to ProjectV2 ${projectV2Id}`);
      }
    }

    console.log("==== Created Issues ====");
    Object.entries(results).forEach(([k,v]) => console.log(`${k} -> #${v.number} ${v.url}`));

  } catch (err) {
    console.error(err);
    process.exit(1);
  }
})();
