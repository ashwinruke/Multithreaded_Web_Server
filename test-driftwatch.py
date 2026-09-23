def run_lookup(user_id):
    query = f"SELECT * FROM users WHERE id = {user_id}"
    cursor.execute(query)
