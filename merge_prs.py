import subprocess
import json
import sys
import time

def run(cmd, check=True):
    print(f"Running: {cmd}")
    res = subprocess.run(cmd, shell=True, text=True, capture_output=True)
    if check and res.returncode != 0:
        print(f"Command failed: {cmd}\nOutput:\n{res.stdout}\n{res.stderr}")
        sys.exit(1)
    return res.stdout.strip()

def run_no_check(cmd):
    print(f"Running: {cmd}")
    res = subprocess.run(cmd, shell=True, text=True, capture_output=True)
    return res.returncode, res.stdout.strip(), res.stderr.strip()

def merge_with_retry(pr):
    for i in range(5):
        code, out, err = run_no_check(f"gh pr merge {pr} --merge --delete-branch")
        if code == 0:
            return True, out, err
        if "is not mergeable" in err or "cannot be cleanly created" in err:
            print(f"Merge not ready, waiting... ({i+1}/5)")
            time.sleep(3)
        else:
            return False, out, err
    return False, out, err

def main():
    run("git stash")
    
    print("Fetching PRs...")
    prs_json = run("gh pr list --state open --limit 200 --json number,createdAt --jq 'sort_by(.createdAt)'")
    if not prs_json:
        print("No open PRs found.")
        return
    try:
        prs = json.loads(prs_json)
    except:
        prs_lines = prs_json.split("\n")
        prs = [int(p) for p in prs_lines if p.strip()]
        prs_sorted = prs
    else:
        prs_sorted = [pr["number"] for pr in prs]
    
    for pr in prs_sorted:
        print(f"\nProcessing PR #{pr}...")
        
        run("git switch main")
        run("git pull origin main || true")

        # if PR is already mergeable directly
        success, out, err = merge_with_retry(pr)
        if success:
            print(f"Successfully merged PR #{pr}.")
            continue
            
        print(f"Direct merge failed for PR #{pr}. Attempting manual resolution...")
        
        run(f"gh pr checkout {pr}")
        
        merge_code, mout, merr = run_no_check("git merge origin/main -m 'Merge origin/main into PR branch'")
        
        if merge_code != 0:
            print("Merge conflict detected.")
            run("git merge --abort")
            # For conflicts, using -X ours or theirs might not solve deletion/modification conflicts.
            # We can use git checkout --ours . then git add .
            out = run("git merge origin/main --no-commit || true", check=False)
            run("git checkout --ours .")
            run("git add .")
            run("git commit -m 'Resolve conflicts'")

        run("git push")
        
        run("git switch main")
        
        success, out, err = merge_with_retry(pr)
        if not success:
            print(f"Still failed to merge PR #{pr} even after manual resolve.\n{out}\n{err}")
            run("git stash pop || true")
            sys.exit(1)
            
    print("Done merging PRs.")
    run("git stash pop || true")
    
if __name__ == "__main__":
    main()
