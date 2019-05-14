FROM python:3.7
MAINTAINER manutres
COPY ./requirements.txt ./webGui.py /code/
COPY ./app /code/app
WORKDIR /code
RUN pip install -r requirements.txt
ENV FLASK_APP=webGui.py
CMD ["flask", "run", "--host=0.0.0.0"]