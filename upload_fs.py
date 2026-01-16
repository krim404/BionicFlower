Import("env")

def before_upload(source, target, env):
    print("Auto-uploading filesystem before firmware...")
    env.Execute("$UPLOADFSARGS")

env.AddPreAction("upload", before_upload)
