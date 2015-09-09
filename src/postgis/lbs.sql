CREATE TABLE user_geo (
  uid SERIAL NOT NULL,
  user_name VARCHAR(32) NOT NULL DEFAULT '',
  geom GEOMETRY(Point, 4326),
  insert_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  update_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (uid)
);

CREATE INDEX geo_gix ON user_geo USING GIST (geom); 

INSERT INTO user_geo (user_name, geom) VALUES ('tom', ST_GeomFromText('POINT(0 0)', 4326));

SELECT uid, user_name FROM user_geo WHERE ST_DWithin(geom, ST_GeomFromText('POINT(0 0)', 4326),1000); 