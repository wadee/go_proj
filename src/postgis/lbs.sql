CREATE TABLE user_geo (
  uid SERIAL NOT NULL,
  user_name VARCHAR(32) NOT NULL DEFAULT '',
  lat double NOT NULL DEFAULT 0,
  lng double NOT NULL DEFAULT 0,
  geom GEOMETRY(Point, 4326),
  insert_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  update_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (uid)
);

CREATE INDEX geo_gix ON user_geo USING gist(geom point_ops); 

INSERT INTO user_geo (user_name, geom) VALUES ('tom', ST_GeomFromText('POINT(0 0)', 4326));

SELECT uid, user_name FROM user_geo WHERE ST_DWithin(geom, ST_GeomFromText('POINT(0 0)', 4326),1000); 

ALTER TABLE user_geo ADD lat double NOT NULL DEFAULT 0;
ALTER TABLE user_geo ADD lng double NOT NULL DEFAULT 0;