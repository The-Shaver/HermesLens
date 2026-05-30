"""Kanban data collector."""

import sqlite3
from datetime import datetime
from pathlib import Path


class KanbanCollector:
    """Collects kanban task data from ~/.hermes/kanban.db.

    Queries the tasks and task_runs tables to extract:
    - Task counts by status (ready, in_progress, blocked, done)
    - Recent tasks with assignee info
    - Tasks currently in progress
    - Recent run history from task_runs table
    """
    
    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.kanban_path = self.hermes_home / "kanban.db"
        self.conn: sqlite3.Connection | None = None
    
    def _get_conn(self) -> sqlite3.Connection:
        """Get a database connection, creating it if needed."""
        if self.conn is None or not self.conn:
            try:
                self.conn = sqlite3.connect(str(self.kanban_path))
            except sqlite3.OperationalError as e:
                # Database might be locked - return None to indicate we can't connect
                pass
        return self.conn
    
    def _close_conn(self):
        """Close the database connection if open."""
        if self.conn and not self.conn.closed:
            try:
                self.conn.close()
            except Exception:
                pass
            finally:
                self.conn = None
    
    def collect(self) -> dict:
        """Collect kanban task data.

        Returns a dictionary with task counts, recent tasks, in-progress tasks, and run history.
        Excludes archived tasks from the main counts.
        """
        default_result = {
            "ready": 0,
            "in_progress": 0,
            "blocked": 0,
            "done": 0,
            "archived": 0,
            "recent": [],
            "in_progress_with_assignee": [],
            "runs_recent": [],
        }
        
        try:
            conn = self._get_conn()
            if conn is None or not conn:
                return default_result
            
            cursor = conn.cursor()
            
            # Count active tasks by status (everything except archived)
            cursor.execute("""
                SELECT 
                    COALESCE(status, '') as status,
                    COUNT(*) as cnt
                FROM tasks 
                WHERE status != 'archived'
                GROUP BY status
            """)
            
            task_counts = {}
            for row in cursor.fetchall():
                task_counts[row[0]] = row[1]
            
            # Count archived tasks separately
            cursor.execute("SELECT COUNT(*) FROM tasks WHERE status = 'archived'")
            archived_count = cursor.fetchone()[0]
            
            return {
                "ready": task_counts.get("ready", 0),
                "in_progress": task_counts.get("in_progress", 0),
                "blocked": task_counts.get("blocked", 0),
                "done": task_counts.get("done", 0),
                "archived": archived_count,
            }
            
        except (sqlite3.Error, IOError) as e:
            return default_result
    
    def collect_all(self) -> dict:
        """Collect all kanban data including recent tasks and runs.

        This is the full collection method that returns everything needed for the status endpoint.
        """
        result = self.collect()  # Get basic counts first
        
        try:
            conn = self._get_conn()
            if conn is None or not conn:
                return result
            
            cursor = conn.cursor()
            
            # Recent tasks (last 5 by created_at desc, excluding archived)
            cursor.execute("""
                SELECT id, title, assignee, status, priority, created_at 
                FROM tasks 
                WHERE status != 'archived'
                ORDER BY created_at DESC 
                LIMIT 5
            """)
            
            recent_tasks = []
            for row in cursor.fetchall():
                task_id = row[0]
                title = row[1][:80] if len(row[1]) > 80 else row[1]  # Truncate long titles
                assignee = row[2] or "unassigned"
                status = row[3]
                priority = row[4]
                created_at = row[5]
                
                recent_tasks.append({
                    "id": task_id,
                    "title": title,
                    "assignee": assignee,
                    "status": status,
                    "priority": priority,
                    "created_at": datetime.fromtimestamp(created_at).isoformat().replace("+00:00", "Z") if created_at else None,
                })
            
            # Tasks currently in_progress with their assignee
            cursor.execute("""
                SELECT id, title, assignee FROM tasks WHERE status = 'in_progress'
            """)
            
            in_progress_tasks = []
            for row in cursor.fetchall():
                task_id = row[0]
                title = row[1][:80] if len(row[1]) > 80 else row[1]
                assignee = row[2] or "unassigned"
                
                in_progress_tasks.append({
                    "id": task_id,
                    "title": title,
                    "assignee": assignee,
                })
            
            # Recent runs from task_runs table (last 5 by started_at desc)
            cursor.execute("""
                SELECT outcome, summary, started_at, profile 
                FROM task_runs 
                ORDER BY started_at DESC 
                LIMIT 5
            """)
            
            recent_runs = []
            for row in cursor.fetchall():
                outcome = row[0] or "unknown"
                summary = str(row[1])[:60] if row[1] else "N/A"
                started_at = int(row[2]) if row[2] is not None else None
                profile = row[3] or "unknown"
                
                recent_runs.append({
                    "outcome": outcome,
                    "summary": summary,
                    "started_at": datetime.fromtimestamp(started_at).isoformat().replace("+00:00", "Z") if started_at else None,
                    "profile": profile,
                })
            
            result["recent"] = recent_tasks
            result["in_progress_with_assignee"] = in_progress_tasks
            result["runs_recent"] = recent_runs
            
        except (sqlite3.Error, IOError) as e:
            # If we can't read detailed data, just return basic counts
            pass
        
        return result
