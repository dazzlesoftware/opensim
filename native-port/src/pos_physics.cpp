// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/pos_physics.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace opensim::scene {
namespace {
using framework::Vector3;using framework::Quaternion;
constexpr float gravity=-9.8f;
float clamp(float x,float min,float max){if(x>max)return max;if(std::isnan(x)||x<min)return min;return x;}
int source_int(float x){if(!std::isfinite(x)||double(x)>=2147483648.||double(x)<-2147483648.)return std::numeric_limits<int>::min();return static_cast<int>(x);}
Quaternion inverse(Quaternion q){float n=(q.x*q.x+q.y*q.y)+(q.z*q.z+q.w*q.w);if(n<1.e-6f)return {0,0,0,1};float m=-1.f/n;return {q.x*m,q.y*m,q.z*m,(0.f-q.w)*m};}
Vector3 rotate(Vector3 v,Quaternion q){float a=q.x+q.x,b=q.y+q.y,c=q.z+q.z;float d=q.w*a,e=q.w*b,f=q.w*c,g=q.x*a,h=q.x*b,i=q.x*c,j=q.y*b,k=q.y*c,l=q.z*c;a=v.x;b=v.y;c=v.z;return {a*(1.f-j-l)+b*(h-f)+c*(i+e),a*(h+f)+b*(1.f-g-l)+c*(k-d),a*(i-e)+b*(k+d)+c*(1.f-g-j)};}
}
std::shared_ptr<PosCharacter> PosPhysicsScene::add_avatar(Vector3 p,Vector3,Vector3,bool flying){auto c=std::make_shared<PosCharacter>();c->position=p;c->flying=flying;characters_.push_back(c);return c;}
std::shared_ptr<PosPrim> PosPhysicsScene::add_prim(Vector3 p,Vector3 s,Quaternion q){auto prim=std::make_shared<PosPrim>(PosPrim{p,s,q});prims_.push_back(prim);return prim;}
void PosPhysicsScene::remove_avatar(const std::shared_ptr<PosCharacter>& c){auto i=std::find(characters_.begin(),characters_.end(),c);if(i!=characters_.end())characters_.erase(i);}
void PosPhysicsScene::remove_prim(const std::shared_ptr<PosPrim>& p){auto i=std::find(prims_.begin(),prims_.end(),p);if(i!=prims_.end())prims_.erase(i);}
float PosPhysicsScene::height_at(Vector3 p)const{auto x=std::bit_cast<std::uint32_t>(source_int(p.x));auto y=std::bit_cast<std::uint32_t>(source_int(p.y));auto index=std::bit_cast<std::int32_t>(y*256u+x);if(index<0||static_cast<std::size_t>(index)>=heights_.size())throw std::out_of_range("POS heightmap index");return heights_[index];}
bool PosPhysicsScene::intersects(const PosCharacter& c,const PosPrim& p){auto q=inverse(p.orientation);auto r=rotate({c.position.x-p.position.x,c.position.y-p.position.y,c.position.z-p.position.z},q);auto s=rotate(c.size(),q);return std::abs(r.x)<(p.size.x*.5+std::abs(s.x))&&std::abs(r.y)<(p.size.y*.5+std::abs(s.y))&&std::abs(r.z)<(p.size.z*.5+std::abs(s.z));}
bool PosPhysicsScene::colliding(const PosCharacter& c)const{for(auto& p:prims_)if(intersects(c,*p))return true;return false;}
        float PosPhysicsScene::simulate(float timeStep)
        {
            
            for (std::size_t i = 0; i < characters_.size(); ++i)
            {
                
                auto& character = *characters_[i];

                float oldposX = character.position.x;
                float oldposY = character.position.y;
                float oldposZ = character.position.z;

                if (!character.flying)
                {
                    character.target_velocity.z += gravity * timeStep;
                }

                Vector3 characterPosition = character.position;

                characterPosition.x += character.target_velocity.x * timeStep;
                characterPosition.y += character.target_velocity.y * timeStep;

                characterPosition.x = clamp(character.position.x, 0.01f, 256 - 0.01f);
                characterPosition.y = clamp(character.position.y, 0.01f, 256 - 0.01f);

                bool forcedZ = false;

                float terrainheight = height_at(character.position);
                if (character.position.z + (character.target_velocity.z * timeStep) < terrainheight + 2)
                {
                    characterPosition.z = terrainheight + character.size_.z;
                    forcedZ = true;
                }
                else
                {
                    characterPosition.z += character.target_velocity.z*timeStep;
                }

                /// this is it -- the magic you've all been waiting for!  Ladies and gentlemen --
                /// Completely Bogus Collision Detection!!!
                /// better known as the CBCD algorithm

                if (colliding(character))
                {
                    characterPosition.z = oldposZ; //  first try Z axis
                    if (colliding(character))
                    {
                        characterPosition.z = oldposZ + character.size_.z / 4.4f; // try harder
                        if (colliding(character))
                        {
                            characterPosition.z = oldposZ + character.size_.z / 2.2f; // try very hard
                            if (colliding(character))
                            {
                                characterPosition.x = oldposX;
                                characterPosition.y = oldposY;
                                characterPosition.z = oldposZ;

                                characterPosition.x += character.target_velocity.x * timeStep;
                                if (colliding(character))
                                {
                                    characterPosition.x = oldposX;
                                }

                                characterPosition.y += character.target_velocity.y * timeStep;
                                if (colliding(character))
                                {
                                    characterPosition.y = oldposY;
                                }
                            }
                            else
                            {
                                forcedZ = true;
                            }
                        }
                        else
                        {
                            forcedZ = true;
                        }
                    }
                    else
                    {
                        forcedZ = true;
                    }
                }

                characterPosition.x = clamp(character.position.x, 0.01f, 256 - 0.01f);
                characterPosition.y = clamp(character.position.y, 0.01f, 256 - 0.01f);

                character.position = characterPosition;

                character.velocity.x = (character.position.x - oldposX)/timeStep;
                character.velocity.y = (character.position.y - oldposY)/timeStep;

                if (forcedZ)
                {
                    character.velocity.z = 0;
                    character.target_velocity.z = 0;
                    character.is_colliding = true;
                    character.request_terse_update();
                }
                else
                {
                    character.is_colliding = false;
                    character.velocity.z = (character.position.z - oldposZ)/timeStep;
                }
            }
            return 1.0f;
        }


}
