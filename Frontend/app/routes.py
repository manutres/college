from flask import request, render_template, json, session, redirect, url_for
from datetime import datetime
import hashlib, requests
from app import app

BASE_URL = "http://192.168.99.100:8081/apiRest/"


#date = datetime.now().strftime("%Y/%m/%d:%H:%M:%S")
#emailPassDate = rcvEmail+rcvPass+date
#tokenTemporal = hashlib.md5(emailPassDate.encode()).hexdigest()

#viewfunctions
@app.route("/", methods=['GET'])
@app.route("/home", methods=['GET'])
def home():
    if 'user' in session:
        return render_template("home.html", user=session['user'])

    return redirect(url_for("login"))

@app.route("/login", methods=['POST', 'GET'])
def login():
    if 'user' in session:
            return redirect("/")

    if request.method == "POST":
        loginReq = {
                    'email': request.form['email'], 
                    'passCifrada': hashlib.md5(request.form['pass'].encode()).hexdigest()
                    }
        response = requests.post(BASE_URL+'user/login', json = loginReq)

        print(response.status_code)
        print(response.json())
        print("despues")

        if response.status_code == 202:
            session['user'] = response.json()
            return redirect(url_for("home"))

    return render_template("login.html")

@app.route("/logout", methods=['POST'])
def logout():
    session.pop('user', None)
    return redirect(url_for('login'))

@app.route("/register", methods=['POST', 'GET'])
def register():
    if 'user' in session:
            return redirect("/")

    if request.method == "POST":
        user = {
                'name':request.form['name'], 
                'lastname': request.form['lastName'],
                'email': request.form['email'],
                'passCifrada': hashlib.md5(request.form['pass'].encode()).hexdigest()
                }
        
        response = requests.post(BASE_URL+'user', json = user)

        # codigo 409 Conflict cuando el usuario ya existe en la applicacion
        if response.status_code == 409 :
            return render_template("register.html")
        else:
            session['user'] = response.json()
            return redirect(url_for("home"))
    else:
        return render_template("register.html")


