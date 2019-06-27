from flask import Flask
from flask_bootstrap import Bootstrap


app = Flask(__name__)
app.secret_key = b'_5#y2L"F4Q8z\n\xec]/'
bootstrap = Bootstrap(app)

from app import routes
