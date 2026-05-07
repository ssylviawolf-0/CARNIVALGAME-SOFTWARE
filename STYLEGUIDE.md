# DATA MANAGEMENT & TEMPLATE STYLE GUIDE

## 1. Directory Structure
- **Hierarchy:** Level 1: Project > Level 2: Life Cycle > Level 3: Time/Wave > Level 4: Content.
- **Naming:** Use lowercase snake_case for all folder names. No spaces or special characters.
- **Balance:** Keep the structure deep enough to be organized but shallow enough to avoid path length limits.

## 2. File Naming Conventions
- **Naming Style:** Strictly use **snake_case** (all lowercase, underscores separating words).
- **Template:** date_project_description_id (e.g., 2024-04-15_mpsi_cleaning-script_01.R)
- **Metadata Delimiter:** Use underscores (_) to separate metadata chunks.
- **Date Format:** YYYY-MM-DD (ISO 8601 standard).
- **Leading Zeros:** Use leading zeros for sequential files (e.g., 01_read-data, 02_clean-data).
- **Abbreviations:**
| Term | Abbreviation |
| :--- | :--- |
| Raw Data | raw |
| Processed/Clean Data | clean |
| Analysis/Results | anly |
| Documentation/Protocol | prot |
| Message Template | tmpl |

## 3. Versioning & Template Lifecycle (AWS Inspired)
- **Drafts vs. Active:** Only files explicitly finalized should be treated as "Active." 
- **Snapshot Principle:** Every commit is a snapshot. Do NOT append version numbers (v1, v2) to filenames.
- **Activation Flow:** 1. **Draft:** Work is performed on a feature branch.
  2. **Saved Version:** A Pull Request is created and merged to `main`.
  3. **Active:** The version on the `main` branch is considered the "Activated" version for production/analysis.
- **History:** Use Git history to compare snapshots rather than creating 'archive' folders.

## 4. Variable & Value Coding
- **Variable Names:** Strictly use **snake_case**. Names must be meaningful and avoid reserved keywords.
- **Booleans:** Standardize binary values (1 = Yes, 0 = No).
- **Missing Data:** Use 'NA' to maintain compatibility across R, Python, and SQL.

## 5. Documentation & Security
- **README:** Every major subdirectory must contain a README.md explaining its contents.
- **Raw Data Integrity:** The 'data/raw' folder is read-only. Never edit these files directly.
- **PII Protection:** Never push files containing Personally Identifiable Information; ensure they are listed in the .gitignore.
